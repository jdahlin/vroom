#ifndef VCC_PP_H
#define VCC_PP_H
/*
 * vcc preprocessor: C (gnu11) translation phase 4, pull model.
 *
 * pp_token_next() hands out one fully macro-expanded token at a time.  Files come
 * from the token cache (lex.h) and are never copied: a file context and an
 * object-like macro body are (pointer, index) into the immutable token
 * arrays.  Only function-like expansions write tokens, into a per-TU arena
 * that is reset whenever the context stack is back at file level.
 *
 * Expansion follows clang's model exactly (it is the parity target): a
 * "disabled" bit per macro while its expansion is on the stack, a paint bit
 * (PP_TOKEN_FLAG_NO_EXPAND) on identifier tokens read while their macro was disabled, and
 * clang's argument pre-expansion, paste and whitespace rules.
 */
#include <setjmp.h>
#include <stdbool.h>
#include <stdarg.h>

#include "frontend/lex/lex.h"

enum {
	PP_TOKEN_KIND_DIRECTIVE_LINE = LEX_TOKEN_KIND_COUNT,	/* a line to pass through: s = "pragma ...", "ident ..." */
};

enum {
	PP_TOKEN_FLAG_NO_EXPAND = 16,		/* painted: never expand (C11 6.10.3.4p2) */
	PP_TOKEN_FLAG_NO_PASTE = 32,	/* a ## that came from an argument */
	PP_TOKEN_FLAG_FROM_MACRO = 64,		/* from a macro expansion (diagnostics) */
	PP_TOKEN_FLAG_EMBED_END = 128,	/* #embed's last byte: -E prints a space after it (clang) */
};

typedef struct ptok {
	const char *s;		/* spelling; raw (with splices) if LEX_TOKEN_FLAG_SPLICED */
	uint32_t len;
	uint32_t id;		/* interned id (LEX_TOKEN_IDENTIFIER) */
	uint32_t loc;		/* TU source location (file base + offset) */
	uint8_t kind, flags;	/* lex_token_kind / PP_TOKEN_KIND_*, LEX_TOKEN_FLAG_* | PP_TOKEN_FLAG_* */
	uint16_t ctr;		/* pp internal: __COUNTER__ digits in the spelling (header cache) */
} ptok;

typedef struct pp_opts {
	const char *main;		/* the source file ("<stdin>" with main_src) */
	const char *main_src;		/* non-NULL: the main file's bytes (stdin), main_len of them */
	size_t main_len;
	const char *const *dirs;	/* search path: -iquote dirs, then -I, -isystem, -idirafter */
	const char *const *edirs;	/* --embed-dir: #embed's search path */
	int nedirs;
	int ndirs, nquote;		/* dirs[0..nquote) only for "" includes */
	int nsys1;			/* dirs[nsys1 - 1..) are system directories (0: none) */
	int xc0, xc1;			/* dirs[xc0..xc1) are extern "C" system directories (clang's ExternCSystem) */
	const char *predef;		/* "<built-in>": #define/#undef/#include lines, run first */
	const char *prefix_from, *prefix_to;	/* -fmacro-prefix-map (one) */
	const char *const *fwdirs;	/* framework dirs: <A/b.h> -> DIR/A.framework/Headers/b.h */
	int nfwdirs;
	int unsigned_char;
	const char *cwd;		/* relative paths are relative to this (NULL: process cwd) */
	int errfd;			/* diagnostics (0: stderr) */
	int asm_mode;			/* assembler-with-cpp: unknown directives pass, # need not stringify */
	int cxx;			/* C++ (its standard's year): true/false in #if, __has_cpp_attribute */
	int target;			/* PP_LINUX_AARCH64, PP_DARWIN, PP_WASM64, PP_WASM32 or PP_LINUX_X86_64 (column of the feature table; x86-64: has_tab_x86) */
	int diag_color;			/* diagnostics: 1 colors, -1 none, 0 when errfd is a terminal */
	int diag_nocaret;		/* -fno-caret-diagnostics: no source lines */
	int diag_show_id;		/* -fdiagnostics-show-id: " [id]" after the flag */
	int diag_format;		/* -fdiagnostics-format=: DIAG_FORMAT_TEXT, _JSON (a record per line), _SARIF */
	int diag_style;			/* -fdiagnostics-style=: DIAG_STYLE_VCC (default), _CLANG, _NARRATED */
	int diag_unicode;		/* -fdiagnostics-text-art-charset=unicode: box-drawing gutter and connectors */
	int diag_parseable;		/* -fdiagnostics-parseable-fixits: clang's fix-it:"file":{l:c-l:c}:"text" lines */
	int diag_patch;			/* -fdiagnostics-generate-patch: the fix-its as a unified diff (GCC's) */
	int fix_apply;			/* --fix: machine-applicable fix-its written to the files */
	struct diag_sink *dsink;		/* vccd's whole-build view of the diagnostics (NULL: none) */
	const char *sarif_file;		/* -fdiagnostics-add-output=sarif:file=F: SARIF there too */
	const char *html_file;		/* -fdiagnostics-add-output=html:file=F: a page of the records */
	char *const *argv;		/* the command line (SARIF's invocation) */
	int argc;
	const char *const *wargs;	/* -W..., -w options in command-line order */
	int nwargs;
	int error_limit;		/* -ferror-limit=: 0 default (20), -1 none */
	int macro_bt_limit;		/* -fmacro-backtrace-limit=: 0 default (6), -1 none */
	int spell_limit;		/* -fspell-checking-limit=: 0 default (50), -1 none; -2 -fno-spell-checking */
	int has_epoch;			/* SOURCE_DATE_EPOCH set: __DATE__/__TIME__/__TIMESTAMP__ from epoch, UTC */
	long long epoch;
	int trigraphs;			/* strict -std (not c23) or -trigraphs: ??x replaced before lexing */
	int c89;			/* -std=c89/-ansi: no // comments */
	int no_digraphs;		/* strict C89/C90, excluding amendment1 (C94) */
	int c23;			/* -std=c23/gnu23: true is 1 in #if */
	int dump_defines;		/* -dD: #define/#undef in the -E text */
	const char *const *dd_order;	/* -dD: clang's order of its predefined macros */
	int ndd_order;
	int comments;			/* -C: comments in the -E text (not in directives or macro arguments) */
	int show_includes;		/* -H: each file entered, dots for its depth, to errfd */
	int deps;			/* -M/-MD: files skipped by guard or #pragma once are dependencies too */
	int fatal_continue;		/* -E text: a fatal error (file not found) does not end the TU, as clang */
	int markers;			/* -E without -P: GNU line markers (# N "file" flags), 2 with -undef */
	unsigned predef_cmdl, predef_incl;	/* ... and its -imacros/-include part */
	int imacros;			/* predef has -imacros (#__include_macros): no header cache */		/* markers: where predef's command-line part (-D/-U) starts */
} pp_opts;

enum { PP_LINUX_AARCH64, PP_DARWIN, PP_WASM64, PP_WASM32, PP_LINUX_X86_64 };
/* WebAssembly, either width (wasm32: ILP32, PLAN-wasm.md W7) */
#define PP_IS_WASM(t) ((t) == PP_WASM64 || (t) == PP_WASM32)

/* set by the front end (a constructor): __has_builtin is 1 for a name it implements */
extern int (*pp_builtin_known)(const char *name);

typedef struct pp pp;

/* NULL if the main file cannot be read or has a fatal error (reported) */
pp *pp_open(const pp_opts *o);
void pp_close(pp *p);
/* fatal errors (missing #include) longjmp here instead of exiting */
void pp_fatal_jump_set(pp *p, jmp_buf *jb);
/* the distinct files read so far, as spelled (for -MD dependency files) */
size_t pp_file_names(const pp *p, const char **names, size_t max, int nosys);
/* the next token; LEX_TOKEN_EOF at the end (and ever after) */
void pp_token_next(pp *p, ptok *t);
/*
 * Spellings of tokens pp made (#x, a##b, __LINE__) live in an arena that
 * pp_token_next reuses once expansion is back at file level.  A consumer that
 * still holds earlier tokens when it asks for the next one (lookahead)
 * brackets that pp_token_next with pp_temporary_hold(p, 1) ... (p, 0).
 */
void pp_temporary_hold(pp *p, int on);
/*
 * -E: the whole TU as clang -E -P prints it (instead of pp_token_next), with the
 * header cache (VPP_NOHC=1: off).  *out stays valid until pp_close.
 * Returns the number of errors.
 */
int pp_text_render(pp *p, const char **out, size_t *len);
/* -dM: the defined macros, sorted, as clang prints them (malloc'ed) */
int pp_macros_dump(pp *p, char **out, size_t *len);
/* header cache counters (process-wide) */
void pp_header_cache_statistics_format(char *buf, size_t n);
/* errors reported so far */
int pp_error_count(const pp *p);
int diag_pragma_active(const pp *p);
int diag_pragma_count(const pp *p);	/* #pragma diagnostic transitions so far */
const char *const *diag_warning_arguments(const pp *p, int *n);	/* the -W/-w options, in order */
const uint32_t *diag_warning_enabled_count_reference(const pp *p);	/* ... of them "warning" or "error": the count, for a test inline */
/* a new build (vccd): forget the include answers; with lex_cache_flush and
 * pp_directory_cache_flush, every file is looked up and read again.  No TU may be running. */
void pp_include_cache_flush(void);

/* is loc in a system header */
int pp_location_is_in_system_header(const pp *p, uint32_t loc);
/* is loc in the main file (not a header, not <built-in>) */
int pp_location_is_in_main_file(const pp *p, uint32_t loc);
/* presumed file name, line and column of a location; 0 if it has none */
int pp_location_describe(const pp *p, uint32_t loc, const char **name, uint32_t *namelen, uint32_t *line, uint32_t *col);
/* a diagnostic at loc ("error", "warning", "note", "fatal error": longjmp/exit) */
void diag_report_va(pp *p, uint32_t loc, const char *what, const char *fmt, va_list ap);

/*
 * The same with clang's source snippet extras: ranges underlined with ~
 * (locations [b, e)), and fix-it hints (replace [b, e) with s; b == e
 * inserts) printed under the caret line.
 */
/*
 * vccd (D7): each top-level record of a TU at its end (record), and with dedup a
 * header warning another TU of this build printed is counted, not rendered (seen:
 * true when key was seen before in this build).
 */
typedef struct diag_sink_record {
	int level;		/* VDL_* */
	const char *flag;	/* NULL: none */
	const char *path;	/* the primary location's file (NULL: none) */
	uint32_t line, col;
	const char *json;	/* the record as -fdiagnostics-format=json writes it, without "rendered" and the
				   newline (NULL once the sink says nojson) */
	size_t jlen;
} diag_sink_record;
typedef struct diag_sink {
	int dedup;
	unsigned ndup;		/* left out in this TU */
	int nojson;		/* set by record(): the rest of the TU's records without JSON */
	bool (*seen)(struct diag_sink *, uint64_t key);
	void (*record)(struct diag_sink *, const diag_sink_record *);
} diag_sink;

typedef struct diag_span { uint32_t b, e; } diag_span;
typedef struct diag_fixit { uint32_t b, e; const char *s; uint8_t app; } diag_fixit;	/* app: VDA_* (0: unspecified) */
typedef struct diag_info {
	const char *what;
	uint32_t loc;
	const diag_span *r;
	int nr;
	const diag_fixit *fx;
	int nfx;
	/* the diagnosed token when it came from a macro expansion (loc: the expansion's): its spelling */
	const char *ms;
	uint32_t mlen;
} diag_info;
void diag_report_extended_va(pp *p, const diag_info *d, const char *fmt, va_list ap);
__attribute__((format(printf, 3, 4)))
void diag_report_extended(pp *p, const diag_info *d, const char *fmt, ...);
/* a warning's state after the -W options: 0 off, 1 warning, 2 error; on: its default
   (flag without "-W"; groups: the warning's groups, NULL-terminated, e.g. "all") */
int diag_warning_state(const pp *p, const char *flag, bool on, const char *const *groups);
/* the last -Werror=/-Wno-error= naming flag (or one of groups): 1 -Werror=, -1 -Wno-error=, 0 none */
int diag_warning_error_named(const pp *p, const char *flag, const char *const *groups);
/* report a warning of flag (clang's " [-Wflag]" / " [-Werror,-Wflag]" suffix); false if off */
__attribute__((format(printf, 6, 7)))
bool diag_warning_report(pp *p, const char *flag, bool on, const char *const *groups, diag_info *d, const char *fmt, ...);
/*
 * Diagnostic records (PLAN-diag.md D1.1).  Every diagnostic is built as a
 * record in the TU's arena and rendered from it (text, JSON, SARIF); the
 * diag_report* calls above are thin wrappers.  A note reported after its
 * diagnostic becomes that diagnostic's child.  Records are only built on the
 * error path; nothing here runs for a TU without diagnostics.
 *
 *   diag_record *d = diag_record_begin(p, DIAG_LEVEL_ERROR, loc, "expected '%s'", "x");
 *   diag_record_label_add(p, d, b, e, "this");		// a range (label NULL: plain ~)
 *   diag_record_fixit_add(p, d, b, e, ";", DIAG_APPLICABILITY_MACHINE);	// replace [b, e)
 *   diag_record_note_add(p, d, loc2, "to match this '('");
 *   diag_record_emit(p, d);
 */
enum { DIAG_LEVEL_NOTE, DIAG_LEVEL_REMARK, DIAG_LEVEL_WARNING, DIAG_LEVEL_ERROR, DIAG_LEVEL_FATAL };
/* a suggestion's applicability (rustc): safe to apply, maybe, has placeholders */
enum { DIAG_APPLICABILITY_UNSPECIFIED, DIAG_APPLICABILITY_MACHINE, DIAG_APPLICABILITY_MAYBE, DIAG_APPLICABILITY_PLACEHOLDER };
#define DIAG_APPLICABILITY_NAME 0x80	/* (or'ed in) the fix changes a macro's name itself, not its body: kept */
typedef struct diag_record_span { uint32_t b, e; const char *label; } diag_record_span;
typedef struct diag_record_fixit { uint32_t b, e; const char *s; uint8_t app; } diag_record_fixit;
typedef struct diag_record diag_record;
struct diag_record {
	diag_record *next;		/* the next record (top level), or the next child */
	diag_record *kids, *klast;	/* notes, in order */
	const char *id;		/* clang's diagnostic name or vcc_*; NULL: none yet */
	const char *fmt;	/* the format the message came from (the id's key) */
	const char *flag;	/* warning flag without "-W", or NULL */
	const char *msg;	/* the message, without the flag */
	const char *label;	/* the primary location's label, or NULL */
	const char *help;	/* an action that fixes it, in words ("= help: ..."), or NULL */
	const char *art;	/* a text-art diagram under the snippet (lines, no final newline), or NULL */
	const void *track;	/* the return address in vcc's reporting function (VCC_TRACK_DIAG) */
	const char *ms;		/* the diagnosed token's spelling when it came from a macro */
	diag_record_span *span;
	diag_record_fixit *fix;
	uint32_t loc;		/* primary location; UINT32_MAX: none */
	uint32_t mlen, nspan, nfix, cspan, cfix;
	uint8_t level;		/* VDL_* as rendered (a -Werror warning is DIAG_LEVEL_ERROR) */
	uint8_t werror;		/* a warning promoted by -Werror */
	uint8_t emitted;
	uint8_t dup;		/* a header warning another TU of this build printed: not rendered */
};
__attribute__((format(printf, 4, 5)))
diag_record *diag_record_begin(pp *p, int level, uint32_t loc, const char *fmt, ...);
diag_record *diag_record_begin_va(pp *p, int level, uint32_t loc, const char *fmt, va_list ap);
void diag_record_label_add(pp *p, diag_record *d, uint32_t b, uint32_t e, const char *label);
void diag_record_fixit_add(pp *p, diag_record *d, uint32_t b, uint32_t e, const char *s, int app);
__attribute__((format(printf, 3, 4)))
void diag_record_help_set(pp *p, diag_record *d, const char *fmt, ...);
/* a diagram shown under the source lines in the vcc style (GCC's text art) */
void diag_record_art_set(pp *p, diag_record *d, const char *s, size_t n);
/* 0: the style shows no diagram; 1: ASCII; 2: Unicode box drawing */
int diag_art_style(const pp *p);
__attribute__((format(printf, 4, 5)))
diag_record *diag_record_note_add(pp *p, diag_record *d, uint32_t loc, const char *fmt, ...);
/* the token spelling when loc is a macro expansion's (clang's caret at the invocation) */
void diag_record_macro_set(diag_record *d, const char *ms, uint32_t mlen);
/* a warning's flag (" [-Wflag]"; werror: " [-Werror,-Wflag]") */
void diag_record_flag_set(diag_record *d, const char *flag);
/* the level a warning of flag is reported at, at loc: DIAG_LEVEL_WARNING, DIAG_LEVEL_ERROR (-Werror), or -1 (off) */
int diag_error_level(pp *p, const char *flag, const char *const *groups, uint32_t loc);
int diag_warning_level(pp *p, const char *flag, bool on, const char *const *groups, uint32_t loc);
/* render (and keep) the record; a fatal error ends the TU */
void diag_record_emit(pp *p, diag_record *d);
/* the TU's records so far, in order (children under kids) */
const diag_record *diag_records_first(const pp *p);
/*
 * VCC_TRACK_DIAG=1 prints with each diagnostic where in vcc it was reported
 * (rustc's -Z track-diagnostics): each reporting function (cc_error, report,
 * diag_report_extended, ...) leaves its return address here, and the record keeps it;
 * it is symbolized (atos) only when printed.  Nothing at the call sites:
 * a __FILE_NAME__:__LINE__ argument or a thread-local store there changed
 * the hot paths' code (+0.6 % / +1.5 % pp).
 */
extern _Thread_local const void *diag_report_site_pc;
extern _Thread_local const char *diag_next_id;
const char *diag_format_id_find(const char *fmt);
/* level name as clang prints it ("error", "fatal error", ...) */
const char *diag_level_name(int level);
enum { DIAG_FORMAT_TEXT, DIAG_FORMAT_JSON, DIAG_FORMAT_SARIF };
/* the text renderers: vcc's (labels, several lines, folded notes, help), clang's
   exactly, and one in words for screen readers (miette's "narratable") */
enum { DIAG_STYLE_VCC, DIAG_STYLE_CLANG, DIAG_STYLE_NARRATED };
/* the end of the TU's diagnostics: the last JSON record, the SARIF log (diag_summary_print calls it) */
void diag_finish(pp *p);
/* clang's location of the end of the main file: its last newline, or its end without one */
uint32_t pp_main_file_end(const pp *p);
uint32_t pp_include_point_find(const pp *p, uint32_t loc);
/* -fspell-checking-limit= as given (0: default, -1: none, -2: off) */
int pp_option_spell_limit(const pp *p);
/* -ferror-limit= as given (0: default, -1: none) */
int pp_option_error_limit(const pp *p);
/* "N errors generated." (clang's summary), if there were any */
void diag_summary_print(pp *p);
/* warnings reported so far */
int diag_warning_count(const pp *p);
/* the end of the token at loc (an identifier, number or punctuator; loc + 1 if unknown) */
uint32_t pp_token_end_find(const pp *p, uint32_t loc);
int pp_location_char(const pp *p, uint32_t loc);
int pp_location_spells(const pp *p, uint32_t loc, const char *s, uint32_t n);
/* the macro (interned id) is defined now */
int pp_macro_id_is_defined(pp *p, uint32_t id);
void pp_token_invalid_report(pp *p, const char *s, uint32_t len, uint32_t loc);
/* the source text from loc b to loc e (one file), NULL if unknown */
const char *pp_source_text(const pp *p, uint32_t b, uint32_t e, size_t *len);
/* from now on a #define in the main file keeps its body in the file's text, so that
   pp_token_spelling_find finds it (VCC_FIELD_USES; the shared copy would not be in a file) */
void pp_macro_text_keep(pp *p);
/* where token t is spelled, for VCC_FIELD_USES (tools renaming names): PP_SPELLING_FILE if its
   bytes are where loc is, PP_SPELLING_MACRO if they are in another file's text (a #define body),
   PP_SPELLING_NONE if in no file (pasted, header cache), PP_SPELLING_DIRTY if it contains a
   line splice.  For FILE and MACRO *file and *offset name the bytes. */
enum { PP_SPELLING_FILE, PP_SPELLING_MACRO, PP_SPELLING_NONE, PP_SPELLING_DIRTY };
struct lex_file;
int pp_token_spelling_find(const pp *p, const ptok *t, const struct lex_file **file, uint32_t *offset);
/* the source text from loc b to the end of its file, NULL if unknown */
const char *pp_source_rest(const pp *p, uint32_t b, size_t *len);
/* loc is on a macro name: where an object-like macro's body tokens are (vcc gives them the
   invocation's location and no PP_TOKEN_FLAG_FROM_MACRO) */
int pp_location_is_in_macro(const pp *p, uint32_t loc);
/* name is a defined macro now */
int pp_macro_name_is_defined(const pp *p, const char *name);

/*
 * Top-level regions (headers included from the main file, cached or being
 * recorded), for the front end's declaration cache.  Positions count the
 * tokens pp_token_next has delivered.  A region's start comes before its first
 * token, its end after its last; e identifies the cached entry (the same
 * pointer means the same tokens), NULL for a region that was not stored.
 */
typedef struct pp_topev {
	const void *e;
	uint64_t pos;
	uint32_t base;		/* location base of the region */
	unsigned ctr;		/* __COUNTER__ at its start */
	uint8_t end, hit;	/* hit: replayed (can be skipped) */
} pp_topev;
/* this TU's events so far; NULL once there were too many */
const pp_topev *pp_top_events(const pp *p, uint32_t *n);
/* tokens delivered so far; tokens pp_top_region_skip dropped */
uint64_t pp_token_count(const pp *p);
uint64_t pp_skipped_token_count(const pp *p);
/* VCC_DRCOV: the header-cache hits of the TU so far, as [first token, end) in delivered positions */
typedef struct pp_hitreg { uint64_t b, e; uint32_t nfile; const void *he; } pp_hitreg;
const pp_hitreg *pp_hit_regions(const pp *p, uint32_t *n);
/* VCC_DRCOV/VCC_DRREC/VCC_DR: the hit whose replay is open (its first token's position, its entry; if base:
   its location base, __COUNTER__ at its start, location span); false if none */
bool pp_hit_open_get(const pp *p, uint64_t *b, const void **he, uint32_t *base, unsigned *ctr, uint32_t *span);
/* the tokens of hit entry he, with its children's */
uint64_t pp_hit_token_count(const void *he);
/* drop the rest of that hit, whose first token was the last one delivered (frontend/c/replay.c); 0 if not so
   (dry: only whether it would) */
int pp_hit_skip(pp *p, int dry);
/*
 * Drop the rest of the replayed region that the last event started, whose
 * first token was the last one delivered: as if it had been delivered and
 * the delivered count stood at its start.  0 if that is not the case.
 */
int pp_top_region_skip(pp *p);

/*
 * Lazy bodies straight from the header cache.  When the token pp_token_next just
 * delivered is a '{' replayed from a cached entry, and its matching '}' is in
 * the same entry's own tokens (no child entry in between), pp_lazy_body_skip moves
 * past the '}' as if the tokens had been delivered and fills z; pp_lazy_body_token_get
 * then hands out '{' .. '}' again, later in the same TU (z is a cursor).
 * The entry is immutable and lives as long as the process.
 */
typedef struct pp_lazy {
	const void *e;
	uint32_t open, close;	/* own-token indices of '{' and '}' */
	uint32_t base;		/* location base of the replay */
	unsigned cbase;		/* __COUNTER__ at the entry's start */
	uint32_t ci;		/* the entry's first __COUNTER__ record after '{' */
	uint32_t k;		/* cursor: next token (pp_lazy_body_token_get) */
	uint32_t rloc;		/* the '}': location and flags */
	uint8_t rflags;
} pp_lazy;
int pp_lazy_body_skip(pp *p, const ptok *lbrace, pp_lazy *z);
void pp_lazy_body_token_get(pp *p, pp_lazy *z, ptok *t);

/* the token's spelling without splices; buf must hold t->len bytes */
const char *pp_token_spell(const ptok *t, char *buf, size_t *len);

#endif
