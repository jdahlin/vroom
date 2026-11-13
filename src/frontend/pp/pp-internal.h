#ifndef VCC_PP_PPIMPL_H
#define VCC_PP_PPIMPL_H
/* the files of vcc/pp/ share this header: what one of them defines and another uses */
/*
 * vcc preprocessor (see pp.h).
 *
 * Layout of the work:
 *
 *   per file, shared by all TUs, built once (pfile): the directive
 *     skeleton -- every directive's token range and, for conditionals, the
 *     index of the next #elif/#else/#endif of the same group, so a false
 *     group is one jump -- and the include guard (controlling macro);
 *   per TU (pp): the macro table indexed by identifier id (two-level, no
 *     hashing; bit 0 of an entry = disabled while expanding), the context
 *     stack, and two arenas: perm (definitions, file names) and tmp
 *     (expansion results, reset whenever only file contexts are left).
 *
 * Expansion is a port of clang's Preprocessor/TokenLexer logic (not
 * Prosser's hide sets), since token-for-token parity with clang -E is the
 * acceptance test: argument pre-expansion only if an argument names a
 * macro, the disable timing of finished contexts (popped before a '(' is
 * read), the paint flag, GNU ", ## __VA_ARGS__", and the leading-space
 * propagation that stringification makes visible.
 */
#include "port/stack-guard.h"
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "frontend/pp/dircache.h"
#include "common/intern.h"
#include "common/fail.h"
#include "frontend/c/keywords.h"
#include "frontend/pp/pp.h"
#include "port/job-regions.h"
#include "common/region.h"

/* functions one file defines and another calls: static when the directory is
   compiled as one TU (VCC_UNITY, the default build), so they stay internal */
#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)

/* ---------------- per-file skeleton ---------------- */

enum {
	PP_DIRECTIVE_OTHER, PP_DIRECTIVE_NULL, PP_DIRECTIVE_IF, PP_DIRECTIVE_IFDEF, PP_DIRECTIVE_IFNDEF, PP_DIRECTIVE_ELIF, PP_DIRECTIVE_ELIFDEF, PP_DIRECTIVE_ELIFNDEF,
	PP_DIRECTIVE_ELSE, PP_DIRECTIVE_ENDIF, PP_DIRECTIVE_DEFINE, PP_DIRECTIVE_UNDEF, PP_DIRECTIVE_INCLUDE, PP_DIRECTIVE_INCLUDE_NEXT, PP_DIRECTIVE_IMPORT,
	PP_DIRECTIVE_PRAGMA, PP_DIRECTIVE_ERROR, PP_DIRECTIVE_WARNING, PP_DIRECTIVE_LINE, PP_DIRECTIVE_LINE_MARKER, PP_DIRECTIVE_IDENT, PP_DIRECTIVE_IMACROS, PP_DIRECTIVE_EMBED,
};

typedef struct pp_directive_span {
	uint32_t tok;	/* the '#' */
	uint32_t end;	/* first token after the line */
	uint32_t next;	/* conditionals: next #elif/#else/#endif of the group */
	uint32_t kind;
} pp_directive_span;

typedef struct pp_macro pp_macro;
typedef struct pp_include_answer pp_include_answer;

typedef struct pp_skeleton {
	const pp_directive_span *dir;
	_Atomic(const pp_macro *) *mac;	/* per directive: the #define's macro, built once */
	_Atomic(const pp_include_answer *) *inc;	/* per directive: #include "x" / <x>, its last answer */
	uint32_t directive_count;
	uint32_t guard;		/* controlling macro id, 0 if none */
	int bad;		/* unbalanced conditionals */
	uint32_t unbalanced_token;	/* ... the directive name's token */
	uint32_t *lines;	/* offsets of '\n', built on first __LINE__ (atomic) */
	uint32_t nlines;
} pp_skeleton;

/* ---------------- arenas ---------------- */

/* ---------------- macros ---------------- */

enum { PP_MACRO_OBJECT_LIKE, PP_MACRO_FUNCTION_LIKE, PP_MACRO_BUILTIN };
enum {
	PP_MACRO_FLAG_VARIADIC = 1, PP_MACRO_FLAG_PASTE = 2, PP_MACRO_FLAG_VA_OPT = 4,
	PP_MACRO_FLAG_BAD_HASH = 8,		/* '#' not before a parameter: an error unless assembler */
	PP_MACRO_FLAG_HAS_HASH = 16,		/* a '#' in the body */
	PP_MACRO_FLAG_UNKNOWN_TOKEN = 32,	/* an unknown token in the body (-Winvalid-pp-token) */
};
enum {
	PP_BUILTIN_NONE, PP_BUILTIN_FILE, PP_BUILTIN_LINE, PP_BUILTIN_COUNTER, PP_BUILTIN_DATE, PP_BUILTIN_TIME, PP_BUILTIN_TIMESTAMP,
	PP_BUILTIN_BASE_FILE, PP_BUILTIN_FILE_NAME, PP_BUILTIN_INCLUDE_LEVEL, PP_BUILTIN_PRAGMA,
	PP_BUILTIN_HAS_ATTRIBUTE, PP_BUILTIN_HAS_BUILTIN, PP_BUILTIN_HAS_FEATURE, PP_BUILTIN_HAS_EXTENSION,
	PP_BUILTIN_HAS_C_ATTRIBUTE, PP_BUILTIN_HAS_CPP_ATTRIBUTE, PP_BUILTIN_HAS_DECLSPEC_ATTRIBUTE, PP_BUILTIN_HAS_WARNING,
	PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, PP_BUILTIN_IS_TARGET_ARCHITECTURE, PP_BUILTIN_IS_TARGET_OS, PP_BUILTIN_IS_TARGET_VENDOR,
	PP_BUILTIN_IS_TARGET_ENVIRONMENT, PP_BUILTIN_IS_TARGET_VARIANT_OS, PP_BUILTIN_IS_TARGET_VARIANT_ENVIRONMENT,
	PP_BUILTIN_BUILDING_MODULE,
	PP_BUILTIN_IS_IDENTIFIER, PP_BUILTIN_HAS_INCLUDE, PP_BUILTIN_HAS_INCLUDE_NEXT, PP_BUILTIN_HAS_EMBED,
};

/*
 * Macro definitions are immutable and shared by all TUs, so that "the same
 * definition" is one pointer (the header cache compares macro table entries
 * by pointer): a #define in a header is built once per directive site
 * (pfile.mac), one in the main file or <built-in> is made canonical by its
 * content (pp_macro_canonical_get), and the builtin macros are process-wide.
 */
struct pp_macro {
	const lex_token *body;
	const lex_token *plist;	/* function-like: "(" ... ")" (plist[0, nplist)), for -dM */
	const char *src;
	const uint8_t *pidx;	/* per body token: parameter index + 1, or 0 */
	uint32_t nbody, name;
	uint32_t hash;		/* pp_macro_canonical_get */
	uint16_t nparam, nplist;
	uint8_t kind, flags, builtin;
};

#define PP_MACRO_PAGE_BITS 11
#define PP_MACRO_PAGE_SIZE (1u << PP_MACRO_PAGE_BITS)
#define PP_MACRO_DIRECTORY_SIZE ((1u << VINTERN_ID_BITS) >> PP_MACRO_PAGE_BITS)

/* ---------------- contexts ---------------- */

enum { PP_CONTEXT_FILE, PP_CONTEXT_LEXED, PP_CONTEXT_TOKENS, PP_CONTEXT_SERVED };

typedef struct pp_context {
	uint8_t type;
	uint8_t barrier;	/* yields LEX_TOKEN_EOF when exhausted instead of popping */
	uint8_t first;		/* 0x80 | SOL/SPACE for the first token (macro contexts) */
	uint8_t offloc;		/* PP_CONTEXT_LEXED: loc = loc + offset (a directive line) */
	uint8_t trailing_space;		/* PP_CONTEXT_TOKENS: ended in an empty argument after a space: the next token gets one */
	uint32_t i, n;
	uint32_t loc;		/* PP_CONTEXT_FILE: file base; PP_CONTEXT_LEXED: expansion loc */
	uint32_t mid;		/* macro to re-enable when popped */
	const lex_token *vt;
	const ptok *pt;
	const struct pp_header_cache_token *ht;	/* PP_CONTEXT_SERVED: the entry's own tokens (pt: its escapes) */
	const char *src;	/* (PP_CONTEXT_SERVED: the shared region's base) */
	/* PP_CONTEXT_FILE */
	const lex_file *f;
	const pp_skeleton *fi;
	uint32_t di;		/* next directive */
	int32_t sdir;		/* search dir it was found in, -1 if none */
	uint32_t fe;		/* fent index */
	const char *spath;	/* the path as clang spells it (f->path is the cache key) */
	uint32_t spelled_directory_length;		/* length of spath's directory part, '/' included */
	/* PP_CONTEXT_SERVED: a header cache entry's tokens (pt, n), its children spliced in */
	const struct pp_header_cache_entry *he;
	uint32_t si, ci;	/* next segment, next __COUNTER__ record */
	unsigned cbase;		/* __COUNTER__ at the entry's start */
	uint8_t top;		/* outermost: owes the entry's pend_out when done (2: a top-level region) */
	uint8_t norec;		/* replayed while recording: its token positions are reserved, not copied */
} pp_context;

typedef struct pp_file_entry {
	uint32_t base;
	const lex_file *f;
	const pp_skeleton *fi;
	const char *name;	/* for __FILE__ (prefix map applied) */
	uint32_t namelen;
	const char *spath;	/* as spelled, for dependency files */
	uint8_t sys;		/* a system header (found in a system directory, or included by one) */
	uint32_t from;		/* the #include's location (0: the main file, <built-in>, or a cached replay) */
	uint32_t diagnostic_state;	/* the #pragma diagnostic state where it was entered (pdnode index) */
	const char *comment_position;	/* -C: its comments before here are printed or dropped (NULL: none yet) */
} pp_file_entry;

#define PP_CONTEXT_MAX (1u << 15)
#define PP_SCRATCH_TOKENS_MAX (1u << 24)

/* the header cache's types (header-cache-internal.h) */
#include "header-cache-internal.h"

/* -pedantic warnings on hot paths (__COUNTER__, every #define of a variadic macro): their
   command-line state, once per TU, so the common case (off, no pragmas) is one test */
enum { PP_WARNING_C2Y, PP_WARNING_VARIADIC_MACRO, PP_WARNING_COUNT };

struct pp {
	uintptr_t *macro_directory[PP_MACRO_DIRECTORY_SIZE];
	const pp_opts *o;
	pp_context *cx;
	int depth, nfile;
	varena perm, tmp;
	bool keep_macro_text;		/* VCC_FIELD_USES: a main-file macro keeps its body in the file text (no canonical copy) */
	bool temporary_held;		/* the consumer holds earlier tokens: tmp is not reset (pp_temporary_hold) */
	ptok *sv;		/* scratch token stack */
	size_t scratch_token_count;
	pp_file_entry *fe;
	uint32_t nfe, next_location_base;
	uint64_t tokens_delivered;	/* tokens pp_token_next delivered */
	uint64_t tokens_skipped;	/* tokens of regions pp_top_region_skip dropped */
	pp_hitreg *hit_regions;		/* VCC_DRCOV: header-cache hits in delivered positions (pp_hit_regions) */
	uint32_t ndrh, open_hit_count;
	struct { uint64_t pos; int depth; const void *he; uint32_t base; unsigned ctr; } open_hits[64];	/* hits whose replay is still open */
	pp_topev *top_events;		/* top-level region events (pp_top_events) */
	uint32_t top_event_count;
	uint8_t tev_full, top_region_recording;
	const void *header_cache_last_entry;	/* the entry pp_header_cache_entry_apply applied last */
	uint8_t pend;		/* SOL/SPACE owed to the next token (empty expansions) */
	uint8_t token_unrecorded;	/* the token pp_token_raw_next() just gave came from a norec replay */
	int argument_preexpansion;		/* in argument pre-expansion */
	int errors;
	unsigned counter;
	const lex_file **once;
	uint32_t nonce, once_file_capacity;
	struct { uint32_t id; uintptr_t m; } *pushed;
	uint32_t pushed_macro_count;
	/* #pragma clang/GCC diagnostic: states are a persistent list (node: one flag's mapping and
	   the state it was set in, 0: the command line's); the transitions are by location */
	struct pp_diagnostic_node { uint32_t prev; uint8_t act; char name[59]; } *dn;
	struct { uint32_t loc, st; } *diagnostic_transitions;
	uint32_t ndn, ndtr, dcur, ndstk, diagnostic_stack[64];
	uint32_t diagnostic_enabling_count;			/* "warning"/"error" nodes: only they turn a warning on */
	struct pp_macro_define_record *macro_definition_sites;		/* every #define's site, by name (built for a macro note) */
	uint32_t nmdx, cmdx, macro_definition_site_files;
	const void *warning_cache_options;		/* the options wc[] was computed for */
	bool wc[PP_WARNING_COUNT];			/* -pedantic warnings on hot paths: on (command line) */
	lex_file builtin;
	const char *base_file_name;
	uint32_t base_file_name_length;
	jmp_buf *fatal_jump;	/* fatal errors end the TU here */
	int color;		/* diagnostics in colour */
	uint32_t diagnostic_last_location;	/* the last diagnostic's location + 1, and whether it was a note */
	bool diagnostic_last_is_note;
	uint32_t diagnostic_last_file;	/* the file of the last diagnostic + 1 (for "In file included from") */
	vdiag *dhead, *diagnostic_tail;	/* the TU's diagnostic records (in perm) */
	bool diagnostic_track;		/* VCC_TRACK_DIAG: vcc's call site after each diagnostic */
	bool diagnostic_finished;		/* pp_diag_finish ran (JSON flushed, SARIF written) */
	vdiag *diagnostic_pending_structured;		/* -fdiagnostics-format=json: the record not yet written */
	vdiag *diagnostic_pending_text;		/* vcc's text style: the record not yet rendered (its notes may follow) */
	uint32_t diagnostic_previous_location;	/* clang's LastLoc + 1 (0: none) and LastLevel: the previous record's */
	uint8_t diagnostic_previous_level;
	struct pp_line_mark {		/* #line / GNU line markers */
		uint32_t fe, off;	/* in fent fe, from byte offset off on */
		uint32_t pline, line;	/* physical line pline is line */
		const char *name;	/* presumed file name, or NULL */
		uint32_t namelen;
	} *lm;
	uint32_t nlm;
	int warns;
	int in_directive;		/* evaluating a directive's tokens (#if, #include, #line) */
	int collecting;		/* in pp_macro_arguments_collect(): directives are inside macro arguments */
	uint32_t coll_loc, collecting_name;	/* the outermost macro collecting: its name token, id */
	/* -E text (pp_text_render) */
	char *out;
	size_t nout, output_capacity;
	unsigned col;
	/* the last token printed, for clang's AvoidConcat: kind, an identifier "L", '.' before it, its end */
	uint8_t pk, pk_L, pk_pp;
	const char *previous_end;
	uint8_t sol_on, edir, cmt_sol;	/* cmt_sol: -C, an empty expansion began the line: a comment next starts it */	/* edir: a directive was printed on this line (clang's EmittedDirectiveOnThisLine) */
	uint32_t cur_line;
	uint8_t emb_sp, embed_tail;	/* -E after #embed: pending, tokens were suffix()/if_empty() */		/* -E: the previous token was #embed's last byte */
	uint8_t output_text_mode;	/* pp_text_render: track lines for the printer */	/* clang's CurLine: the presumed line the output is on */
	int line_start_context_depth;
	uint32_t expansion_location;
	uint32_t line_start_location;	/* the source macro name a line starts with: expanded tokens' column */
	/* -E without -P: GNU line markers */
	uint8_t lmark, lm_blk, marker_is_system;	/* on; <built-in>'s block printed; the current file is a system header */
	uint32_t lm_n, marker_builtin_line;	/* predefined macros before the command line (+1); <built-in>'s line */
	const char *marker_file_name;		/* the current file's presumed name */
	uint32_t marker_file_name_length;
	struct { uint32_t id; const pp_macro *m; } *define_events;	/* -dD: <built-in>'s #define/#undef, until printed */
	uint32_t ndd, define_event_capacity;
	int define_events_printed;
	/* header cache */
	int hc;			/* on (pp_text_render or pp_token_next, not disabled) */
	int output_mode;		/* 0 unset, 1 text (pp_text_render), 2 tokens (pp_token_next) */
	uint32_t sig;		/* base signature (PP_HEADER_CACHE_NONE: no cache) */
	uint32_t *psig;		/* search path prefix ids, [0, ndirs] */
	struct pp_recording {
		int on;		/* n > 0 */
		int n, nbad;	/* levels; levels [0, nbad) are not cacheable */
		int uncacheable_line;
		uint32_t serial;
		pp_header_cache_level levels[PP_HEADER_CACHE_LEVEL_MAX];
		pp_header_cache_touch_record *t;
		uint32_t nt;
		pp_header_cache_once_mark *ord;
		uint32_t nord;
		pp_header_cache_counter_position *ctr;
		uint32_t nctr;
		pp_header_cache_counter_read *crd;
		uint32_t ncrd;
		pp_header_cache_recorded_segment *seg;
		uint32_t nseg;
		pp_header_cache_recorded_child *kid;	/* child entries, in order (their reads and writes are theirs) */
		uint32_t nkid;
		uint32_t *kex;
		uint32_t child_exclusion_count;
		pp_header_cache_lookup *lk;	/* the levels' own lookups (hlook) */
		uint32_t nlk;
		pp_header_cache_counter_provenance *cp;	/* indexed by ptok.ctr */
		uint32_t counter_provenance_count;
		ptok *tk;	/* tokens handed out (token mode) */
		uint32_t ntk;
		char *sb;	/* their spellings that live in per-TU memory */
		size_t spelling_bytes;
	} rec;
	uint64_t *rst[PP_MACRO_DIRECTORY_SIZE];	/* per id: serial << 32 | index in rec.t */
	struct { const void *e; uint32_t gen, ok; } hm[4096];	/* child entry verdicts in one pp_header_cache_entry_serve */
	uint32_t hgen;
	size_t entry_bytes;	/* what the entry being published allocated */
	const void *blob_cache[4096];	/* recent spelling blobs, by hash */
	const char *punctuator_blobs[256];	/* punctuator spelling blobs, by kind */
	const pp_header_cache_chunk *chunk_cache_by_hash[4096];
	const pp_header_cache_chunk *chunk_cache_by_ids[4096];	/* recent chunks, by (first id, last id, length) */	/* recent chunks, by hash (most repeats are a child's) */
};

#define PP_ERROR(p, loc, ...) pp_diag(p, loc, "error", __VA_ARGS__)
#define PP_FATAL(p, loc, ...) pp_diag(p, loc, "fatal error", __VA_ARGS__)
#define PP_WARN(p, loc, ...) pp_diag(p, loc, "warning", __VA_ARGS__)

/* ---------------- header cache: recording reads and writes ---------------- */

/* the open regions are not cacheable (something in them depends on more than we record) */
VPRIV void diag_pragma(pp *p, const char *q, const char *e, uint32_t loc);
VPRIV void diag_file_end(pp *p, uint32_t fe);
#define PP_HEADER_CACHE_REGION_REJECT(p) do { if ((p)->rec.n) { (p)->rec.nbad = (p)->rec.n; (p)->rec.uncacheable_line = __LINE__; } } while (0)

VPRIV void pp_wc_fill(pp *p);

/* false: warning k cannot be reported in this TU (off on the command line, no pragmas) */
static inline bool pp_warning_is_possible(pp *p, int k)
{
	if (p->warning_cache_options != p->o)
		pp_wc_fill(p);
	return p->wc[k] || p->diagnostic_enabling_count;
}

/* ---------------- #if expressions ---------------- */

typedef struct pp_if_value {
	uint64_t v;
	bool u;
} pp_if_value;

/* ---------------- across files ---------------- */
/* translation-unit.c */
VPRIV void *pp_address_space_reserve(size_t n);
static inline int pp_unsigned_format(char *b, unsigned long long v);
/* next-token.c */
VPRIV void pp_header_cache_mode_set(pp *p, int mode);
/* file.c */
extern _Atomic size_t pp_shared_region_used;
VPRIV void *pp_shared_allocate(size_t n);
VPRIV const char *pp_shared_base(void);
VPRIV const pp_skeleton *pp_skeleton_get(const lex_file *f, bool *fresh);
VPRIV uint32_t pp_file_line_of_offset(const lex_file *f, const pp_skeleton *cfi, uint32_t off);
/* header-cache-store.c */
VPRIV void *pp_header_cache_allocate(pp *p, size_t n);
VPRIV pp_header_cache_vector pp_header_cache_vector_store(pp *p, const void *a, uint32_t n, uint32_t esz);
VPRIV const void *pp_header_cache_blob_get(pp *p, const void *s, size_t n);
/* text-output.c: line markers, comments, the output buffer */
VPRIV bool pp_output_line_move(pp *p, uint32_t line, bool req);
VPRIV void pp_output_newline(pp *p);
VPRIV void pp_comments_flush(pp *p, pp_context *c, const char *upto);
VPRIV __attribute__((cold)) void pp_comments_skip_to(pp *p, const ptok *rp);
VPRIV const char *pp_comment_line_end(const char *s);
VPRIV unsigned pp_token_column(const pp *p, uint32_t loc);
VPRIV void pp_output_file_change(pp *p, const pp_context *from, int enter);
VPRIV void pp_line_marker_builtin_count(pp *p);
VPRIV void pp_line_marker_file_enter(pp *p, const pp_file_entry *e);
VPRIV void pp_line_marker_file_rename(pp *p, const char *name, uint32_t nlen, uint32_t line);
extern _Thread_local char *pp_output_spare;
extern _Thread_local size_t pp_output_spare_capacity;
VPRIV void pp_output_reserve(pp *p, size_t n);
static inline void pp_output_append(pp *p, const char *s, size_t n);
static inline void pp_output_append_character(pp *p, char ch);
/* diag/: locations, diagnostics and their rendering; pp_directive_extra_token_warn is directive.c's */
VPRIV uint32_t loc_line(const pp *p, uint32_t loc);
VPRIV const char *presumed_name(const pp *p, uint32_t fe, uint32_t loc, uint32_t *len);
VPRIV __attribute__((format(printf, 4, 5))) void pp_diag(pp *p, uint32_t loc, const char *what, const char *fmt, ...);
void pp_diagx(pp *p, const pp_dinfo *d, const char *fmt, ...);
bool pp_warnx(pp *p, const char *flag, bool on, const char *const *groups, pp_dinfo *d, const char *fmt, ...);
VPRIV bool obj_macro_at(pp *p, uint32_t loc);
VPRIV void pp_directive_extra_token_warn(pp *p, const pp_context *c, uint32_t name, uint32_t i);
void pp_diag_summary(pp *p);
VPRIV const pp_file_entry *loc_fent(const pp *p, uint32_t loc);
VPRIV bool loc_ok(const pp *p, uint32_t loc, const pp_file_entry **e);
VPRIV int diag_where(const pp *p, uint32_t loc, const char **name, uint32_t *nl, uint32_t *line, uint32_t *col);
struct vbuffer;
VPRIV void render_text(pp *p, struct vbuffer *b, const vdiag *d);
VPRIV bool render_head(pp *p, struct vbuffer *b, const vdiag *d, const pp_file_entry **ep);
VPRIV void parseable_fixits(pp *p, struct vbuffer *b, const vdiag *d);
VPRIV uint32_t dcols(const char *s, uint32_t b, uint32_t e, uint32_t col);
VPRIV void db_c(struct vbuffer *b, char c, int n);
VPRIV void include_stack(pp *p, struct vbuffer *b, const pp_file_entry *e);
/* diagtext.c: vcc's text style (records rendered when the next one starts, so that
   their notes can fold into the snippet), the narrated style, the type diff */
VPRIV void diag_text_flush(pp *p);
VPRIV void diag_narrate(pp *p, struct vbuffer *b, vdiag *d);
VPRIV void diag_render_rec(pp *p, struct vbuffer *b, vdiag *d);
VPRIV void diag_render_color(pp *p, struct vbuffer *b, vdiag *d);
VPRIV void diag_typediff(struct vbuffer *b, const vdiag *d, const char *plain);
/* moved apart by the split of the diagnostics engine, still one unity TU */
VPRIV __attribute__((noinline, cold)) void dsink_records(pp *p);
VPRIV void jstr(struct vbuffer *b, const char *s, size_t n);
VPRIV void jcstr(struct vbuffer *b, const char *s);
VPRIV void html_page(pp *p, struct vbuffer *b);
VPRIV void sarif_log(pp *p, struct vbuffer *b);
VPRIV int level_of(const char *what);
VPRIV void kid_add(vdiag *d, vdiag *k);
VPRIV void kid_after(vdiag *top, vdiag *after, vdiag *n);
VPRIV void render_clang(pp *p, struct vbuffer *b, vdiag *d);
VPRIV void track_str(struct vbuffer *b, const void *pc);
VPRIV void render_narr(pp *p, struct vbuffer *b, const vdiag *d);
VPRIV uint32_t lstart(const char *s, uint32_t off);
VPRIV uint32_t lend(const char *s, uint32_t size, uint32_t ls);
VPRIV bool fix_in_macro(const pp *p, uint32_t b, uint32_t e);
VPRIV void macro_adjust(pp *p, vdiag *top, vdiag *d);
/* diagout.c: JSON lines and SARIF */
VPRIV void diag_json_flush(pp *p);
VPRIV void diag_fixits_out(pp *p);
/* include.c: dependencies */
VPRIV uint8_t pp_include_system_kind(pp *p, int32_t sdir);
VPRIV void pp_dependency_add(pp *p, const char *path, uint8_t sys);

/* macro-dump.c: -dD */
VPRIV void pp_macro_define_event_record(pp *p, uint32_t id, const pp_macro *m, bool builtin);
VPRIV void pp_macro_define_events_flush(pp *p);

/* macro table: inline, below */
static inline uintptr_t pp_macro_table_get(const pp *p, uint32_t id);
static inline uintptr_t *pp_macro_table_slot(pp *p, uint32_t id);
static inline void pp_macro_table_set(pp *p, uint32_t id, const pp_macro *m);
static inline void pp_macro_table_disable(pp *p, uint32_t id);
static inline void pp_macro_table_enable(pp *p, uint32_t id);
/* header-cache-record.c */
VPRIV uint64_t *pp_header_cache_stamp_slot(pp *p, uint32_t id);
static inline pp_header_cache_touch_record *pp_header_cache_touch(pp *p, uint32_t id, bool *isnew);
static inline void pp_header_cache_touch_read(pp *p, uint32_t id, uintptr_t v, uint32_t g);
static inline void pp_header_cache_touch_write(pp *p, uint32_t id);
VPRIV __attribute__((noinline)) void pp_header_cache_read_record(pp *p, uint32_t id, uintptr_t e, int def);
VPRIV uint16_t pp_header_cache_counter_provenance_new(pp *p, uint32_t val, uint32_t off, uint32_t len, uint16_t next);
VPRIV uint16_t pp_header_cache_counter_provenance_chain(pp *p, uint16_t a, uint32_t d, uint16_t tail);
VPRIV __attribute__((noinline)) void pp_header_cache_counter_read_record(pp *p, const ptok *t, uintptr_t e);
static inline uintptr_t pp_macro_read_token(pp *p, const ptok *t, int def);
static inline uintptr_t pp_macro_read(pp *p, uint32_t id);
static inline bool pp_macro_is_defined(pp *p, uint32_t id);
static inline bool pp_macro_guard_is_defined(pp *p, uint32_t id);
VPRIV void pp_macro_define_record(pp *p, uint32_t id, const pp_macro *m);
/* header-cache-statistics.c */
extern pp_header_cache_statistics_counters pp_header_cache_statistics;
/* header-cache-key.c */
extern size_t pp_header_cache_memory_limit;
VPRIV bool pp_header_cache_signatures_get(const pp_opts *o, uint32_t *sig, uint32_t *psig);
VPRIV pp_header_cache_key *pp_header_cache_key_get(uint32_t sig, const lex_file *f, int32_t sdir, uint32_t nfile, const char *spath, uint8_t sys, bool make);
/* token.c */
static inline void pp_token_fill_from_lexer(ptok *t, const lex_token *v, const char *src, uint32_t loc);
const char *pp_token_spell(const ptok *t, char *buf, size_t *len);
VPRIV char *pp_token_spell_to(char *dst, const ptok *t);
static inline char *pp_token_spell_to_padded(char *dst, const ptok *t);
static inline void pp_scratch_token_push(pp *p, const ptok *t);
VPRIV ptok *pp_scratch_tokens_take(pp *p, size_t base, uint32_t *n);
static inline pp_context *pp_context_push(pp *p);
VPRIV void pp_context_tokens_push(pp *p, const ptok *t, uint32_t n, uint32_t mid, uint8_t first, int barrier);
VPRIV void pp_context_directive_line_push(pp *p, const pp_context *fc, uint32_t b, uint32_t e);
VPRIV void pp_context_pop_to(pp *p, int d);
#define PP_COVERAGE_HIT_MAX 65536
/* next-token.c: VCC_DRCOV and friends, read once */
extern int pp_coverage_enabled;
/* header-skip.c */
VPRIV void pp_coverage_hit_begin(pp *p, int d0);
VPRIV void pp_coverage_hit_end(pp *p);
VPRIV void pp_top_event_record(pp *p, const void *e, uint32_t base, unsigned ctr, uint8_t end, uint8_t hit);
/* token.c */
VPRIV __attribute__((noinline)) void pp_token_raw_next(pp *p, ptok *t);
static inline __attribute__((always_inline)) bool pp_token_raw_plain_next(pp *p, pp_context *c, ptok *t);
VPRIV __attribute__((noinline)) void pp_token_raw_pop(pp *p, ptok *t);
static inline __attribute__((always_inline)) void pp_token_raw_fast_next(pp *p, ptok *t);
VPRIV __attribute__((noinline)) void pp_token_expanded_next_slow(pp *p, ptok *t, uintptr_t e);
static inline __attribute__((always_inline)) void pp_token_expanded_next(pp *p, ptok *t);
/* expand.c */
VPRIV void pp_macro_note_at(pp *p, const pp_macro *m, uint32_t before, const char *msg);
VPRIV bool pp_kind_is_string_literal(int k);
extern const uint8_t pp_identifier_character_table[256];
static inline bool pp_character_is_identifier(unsigned char c);
VPRIV int pp_macro_expand(pp *p, const pp_macro *m, ptok *t);
/* builtin.c */
VPRIV const pp_context *pp_file_context_current(const pp *p);
VPRIV bool pp_header_name_read(pp *p, char *buf, size_t cap, size_t *len, bool *angled, ptok *t);
VPRIV int pp_builtin_macro_expand(pp *p, const pp_macro *m, ptok *t);
/* ifexpr.c */
VPRIV pp_if_value pp_number_parse(pp *p, const ptok *t);
VPRIV bool pp_if_evaluate(pp *p, const pp_context *fc, uint32_t b, uint32_t e);
VPRIV bool pp_if_tokens_evaluate(pp *p, const ptok *t, uint32_t n, uint64_t *v);
/* include.c: pstrdup, used by embed.c; embed.c: #embed */
VPRIV char *pp_string_copy(pp *p, const char *s, size_t n);
VPRIV void pp_embed_run(pp *p, pp_context *c, uint32_t b, uint32_t e, uint32_t loc, uint32_t nloc);
VPRIV int pp_embed_has_query(pp *p, const pp_context *fc, uint32_t loc);
/* directive.c */
VPRIV const char *pp_define_parse(const lex_token *vt, uint32_t b, uint32_t e, pp_macro *m, void *(*mem)(void *, size_t), void *mc, uint32_t *eoff);
VPRIV void *pp_memory_permanent_allocate(void *a, size_t n);
VPRIV void *pp_memory_shared_allocate(void *a, size_t n);
/* macro.c */
extern const lex_token pp_macro_empty_body[1];
VPRIV void pp_macro_define(pp *p, pp_context *c, uint32_t b, uint32_t e);
/* include.c */
VPRIV const lex_file *pp_path_try(const pp_opts *o, const char *path, size_t len);
VPRIV const lex_file *pp_include_find(pp *p, const pp_context *from, const char *name, size_t nlen, bool angled, bool next, int32_t *sdir, const char **path, const pp_include_answer **ie);
VPRIV bool pp_header_cache_lookups_match(pp *p, const pp_header_cache_entry *e);
/* header-cache-key.c: the #pragma once set; header-cache-validate.c; header-cache-serve.c */
VPRIV int pp_once_file_find(const pp *p, const lex_file *f);
VPRIV void pp_once_file_add(pp *p, const lex_file *f);
VPRIV bool pp_header_cache_region_is_allowed(const pp *p);
VPRIV bool pp_header_cache_entry_is_valid(pp *p, const pp_header_cache_entry *e);
VPRIV void pp_header_cache_text_serve(pp *p, const pp_header_cache_entry *e, unsigned base);
static inline void pp_header_cache_text_start(pp *p, uint8_t sol);
/* header-cache-serve.c, header-cache-record.c, header-cache-publish.c */
VPRIV bool pp_header_cache_serve_child(pp *p, pp_context *c);
VPRIV void pp_header_cache_serve_token(pp *p, pp_context *c, ptok *t);
VPRIV void pp_header_cache_token_record(pp *p, const ptok *t);
VPRIV bool pp_header_cache_entry_serve(pp *p, pp_header_cache_key *k);
extern _Thread_local ptok *pp_recording_spare_tokens;
extern _Thread_local char *pp_recording_spare_spellings;
extern _Thread_local pp_recording_spare_buffer_set pp_recording_spare_buffers;
VPRIV void pp_header_cache_region_begin(pp *p, pp_header_cache_key *k);
VPRIV void pp_header_cache_region_end(pp *p);
VPRIV void pp_header_cache_child_attach(pp *p, const pp_header_cache_entry *e);
/* include.c */
VPRIV void pp_file_enter(pp *p, const lex_file *f, const char *path, int32_t sdir);
VPRIV void pp_include_run(pp *p, pp_context *c, uint32_t b, uint32_t e, int kind, uint32_t loc);
/* directive.c */
VPRIV int pp_pragma_run(pp *p, const char *s, size_t n, uint32_t loc, ptok *out, uint32_t sloc);
VPRIV int pp_directive_run(pp *p, pp_context *c, ptok *out);
VPRIV bool pp_pad_at(const char *s, uint32_t ls, uint32_t le, uint32_t x);

/* decimal digits of v at b (no terminator); returns the length */
static inline int pp_unsigned_format(char *b, unsigned long long v)
{
	char t[24];
	int n = 0;

	do
		t[n++] = (char)('0' + v % 10);
	while (v /= 10);
	for (int i = 0; i < n; i++)
		b[i] = t[n - 1 - i];
	return n;
}

/* ---------------- macro table ---------------- */

static inline uintptr_t pp_macro_table_get(const pp *p, uint32_t id)
{
	const uintptr_t *pg = p->macro_directory[id >> PP_MACRO_PAGE_BITS];

	return pg ? pg[id & (PP_MACRO_PAGE_SIZE - 1)] : 0;
}

static inline uintptr_t *pp_macro_table_slot(pp *p, uint32_t id)
{
	uintptr_t **pg = &p->macro_directory[id >> PP_MACRO_PAGE_BITS];

	if (unlikely(!*pg)) {
		*pg = varena_alloc(&p->perm, PP_MACRO_PAGE_SIZE * sizeof(uintptr_t));
		memset(*pg, 0, PP_MACRO_PAGE_SIZE * sizeof(uintptr_t));
	}
	return &(*pg)[id & (PP_MACRO_PAGE_SIZE - 1)];
}

static inline void pp_macro_table_set(pp *p, uint32_t id, const pp_macro *m)
{
	*pp_macro_table_slot(p, id) = (uintptr_t)m;
}

/* id is a macro being expanded: its page exists (it was read, defined) */
static inline void pp_macro_table_disable(pp *p, uint32_t id) { p->macro_directory[id >> PP_MACRO_PAGE_BITS][id & (PP_MACRO_PAGE_SIZE - 1)] |= 1; }
static inline void pp_macro_table_enable(pp *p, uint32_t id) { p->macro_directory[id >> PP_MACRO_PAGE_BITS][id & (PP_MACRO_PAGE_SIZE - 1)] &= ~(uintptr_t)1; }

/* the innermost level's touch entry for id (new if *isnew) */
static inline pp_header_cache_touch_record *pp_header_cache_touch(pp *p, uint32_t id, bool *isnew)
{
	uint64_t *s = pp_header_cache_stamp_slot(p, id);
	uint32_t ser = p->rec.levels[p->rec.n - 1].serial;

	if ((uint32_t)(*s >> 32) == ser) {
		*isnew = false;
		return &p->rec.t[(uint32_t)*s];
	}
	if (unlikely(p->rec.nt == PP_HEADER_CACHE_TOUCH_MAX)) {
		PP_HEADER_CACHE_REGION_REJECT(p);
		*isnew = false;
		return NULL;
	}
	*isnew = true;
	p->rec.t[p->rec.nt] = (pp_header_cache_touch_record){ id, 0, 0, *s };
	*s = (uint64_t)ser << 32 | p->rec.nt;
	return &p->rec.t[p->rec.nt++];
}

/* id read with value v at entry of the innermost level (v: enabled, or PP_HEADER_CACHE_DEFINED) */
static inline void pp_header_cache_touch_read(pp *p, uint32_t id, uintptr_t v, uint32_t g)
{
	bool isnew;
	pp_header_cache_touch_record *t = pp_header_cache_touch(p, id, &isnew);

	if (!t)
		return;
	if (isnew) {
		t->flags = PP_HEADER_CACHE_TOUCH_READ | g;
		t->v = v;
	} else if (v != PP_HEADER_CACHE_DEFINED && (t->flags & PP_HEADER_CACHE_TOUCH_READ) && t->v == PP_HEADER_CACHE_DEFINED && !(t->flags & PP_HEADER_CACHE_TOUCH_WRITE)) {
		t->v = v;	/* defined-only so far; unwritten, so this is the entry value */
	}
}

static inline void pp_header_cache_touch_write(pp *p, uint32_t id)
{
	bool isnew;
	pp_header_cache_touch_record *t = pp_header_cache_touch(p, id, &isnew);

	if (t)
		t->flags |= PP_HEADER_CACHE_TOUCH_WRITE | PP_HEADER_CACHE_TOUCH_OWN;
}

/* the entry for token t's identifier, as an expansion reads it */
static inline uintptr_t pp_macro_read_token(pp *p, const ptok *t, int def)
{
	uintptr_t e = pp_macro_table_get(p, t->id);

	if (unlikely(p->rec.on)) {
		if (t->ctr)
			pp_header_cache_counter_read_record(p, t, e);
		else
			pp_header_cache_read_record(p, t->id, e, def);
	}
	return e;
}

/* the entry for id, as a macro expansion reads it */
static inline uintptr_t pp_macro_read(pp *p, uint32_t id)
{
	uintptr_t e = pp_macro_table_get(p, id);

	if (unlikely(p->rec.on))
		pp_header_cache_read_record(p, id, e, 0);
	return e;
}

/* is id defined (#ifdef, defined(), include guards) */
static inline bool pp_macro_is_defined(pp *p, uint32_t id)
{
	uintptr_t e = pp_macro_table_get(p, id);

	if (unlikely(p->rec.on))
		pp_header_cache_read_record(p, id, e, 1);
	return e != 0;
}

/* an include guard: defined? */
static inline bool pp_macro_guard_is_defined(pp *p, uint32_t id)
{
	uintptr_t e = pp_macro_table_get(p, id);

	if (unlikely(p->rec.on))
		pp_header_cache_read_record(p, id, e, 2);
	return e != 0;
}

static inline void pp_output_append(pp *p, const char *s, size_t n)
{
	pp_output_reserve(p, n);
	memcpy(p->out + p->nout, s, n);
	p->nout += n;
}

static inline void pp_output_append_character(pp *p, char ch)
{
	pp_output_reserve(p, 1);
	p->out[p->nout++] = ch;
}

/* three stores: s, len|id, loc|kind|flags|ctr (little endian) */
static inline void pp_token_fill_from_lexer(ptok *t, const lex_token *v, const char *src, uint32_t loc)
{
	uint64_t li, lk;
	uint32_t kf;

	memcpy(&li, &v->len, 8);
	memcpy(&kf, &v->kind, 4);
	kf &= 0xffu | (uint32_t)(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE | LEX_TOKEN_FLAG_SPLICED | LEX_TOKEN_FLAG_RAW_STRING) << 8;	/* ctr = 0 */
	lk = loc | (uint64_t)kf << 32;
	t->s = src + v->off;
	memcpy(&t->len, &li, 8);
	memcpy(&t->loc, &lk, 8);
}

/*
 * pp_token_spell_to() where dst has 16 bytes of room past the spelling: a short clean
 * spelling is copied as 16 bytes (spellings are readable 16 bytes past their
 * end: file text is padded, and so is every spelling pp makes)
 */
static inline char *pp_token_spell_to_padded(char *dst, const ptok *t)
{
	if (likely(!(t->flags & LEX_TOKEN_FLAG_SPLICED) && t->len <= 16)) {
		memcpy(dst, t->s, 16);
		return dst + t->len;
	}
	return pp_token_spell_to(dst, t);
}

static inline void pp_scratch_token_push(pp *p, const ptok *t)
{
	if (unlikely(p->scratch_token_count == PP_SCRATCH_TOKENS_MAX))
		PP_FATAL(p, 0, "macro expansion too large");
	p->sv[p->scratch_token_count++] = *t;
}

static inline pp_context *pp_context_push(pp *p)
{
	pp_context *c;

	if (unlikely(p->depth == PP_CONTEXT_MAX))
		PP_FATAL(p, 0, "macro expansion or #include nesting too deep");
	c = &p->cx[p->depth++];
	memset(c, 0, offsetof(pp_context, f));
	return c;
}

/* the next token of the top context if it is plain (nothing owed, no directive) */
static inline __attribute__((always_inline)) bool pp_token_raw_plain_next(pp *p, pp_context *c, ptok *t)
{
	if (likely(c->i < c->n && !(c->first | p->pend))) {
		if (likely(c->type == PP_CONTEXT_TOKENS)) {
			*t = c->pt[c->i++];
			return true;
		}
		if (c->type == PP_CONTEXT_LEXED) {
			const lex_token *v = &c->vt[c->i++];

			pp_token_fill_from_lexer(t, v, c->src, c->offloc ? c->loc + v->off : c->loc);
			return true;
		}
		if (c->type == PP_CONTEXT_FILE) {
			const lex_token *v = &c->vt[c->i];

			if (likely(!((v->flags & LEX_TOKEN_FLAG_LINE_START) && v->kind == LEX_TOKEN_HASH))) {
				c->i++;
				pp_token_fill_from_lexer(t, v, c->src, c->loc + v->off);
				return true;
			}
		}
	}
	return false;
}

/* pp_token_raw_next(), inline: a macro context's next token with nothing owed (most tokens) */
static inline __attribute__((always_inline)) void pp_token_raw_fast_next(pp *p, ptok *t)
{
	if (likely(pp_token_raw_plain_next(p, &p->cx[p->depth - 1], t)))
		return;
	pp_token_raw_pop(p, t);
}

/* the next fully expanded token */
VPRIV __attribute__((noinline, cold)) void pp_dollar_split(pp *p, ptok *t);

static inline __attribute__((always_inline)) void pp_token_expanded_next(pp *p, ptok *t)
{
	uintptr_t e;

	pp_token_raw_fast_next(p, t);
	if (t->kind != LEX_TOKEN_IDENTIFIER || (t->flags & PP_TOKEN_FLAG_NO_EXPAND))
		return;
	e = pp_macro_read_token(p, t, 0);
	if (likely(!e)) {
		if (unlikely(p->o->asm_mode) && t->s[0] == '$')
			pp_dollar_split(p, t);
		return;
	}
	pp_token_expanded_next_slow(p, t, e);
}

static inline __attribute__((always_inline)) void pp_token_next_resetting_temporary(pp *p, ptok *t)
{
	if (p->depth == p->nfile && !p->scratch_token_count && !p->temporary_held)
		p->tmp.used = 0;	/* nothing above file level refers to tmp */
	pp_token_expanded_next(p, t);
}

static inline bool pp_character_is_identifier(unsigned char c)
{
	return pp_identifier_character_table[c];
}

/* the first output of the open levels that have none yet starts here */
static inline void pp_header_cache_text_start(pp *p, uint8_t sol)
{
	for (int i = p->rec.n - 1; i >= 0 && !p->rec.levels[i].text_started; i--) {
		p->rec.levels[i].text_started = 1;
		p->rec.levels[i].first_sol = sol;
		p->rec.levels[i].text_at_start = p->nout;
	}
}

#endif
