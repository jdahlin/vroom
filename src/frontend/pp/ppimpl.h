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
#include "port/vstack.h"
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
#include "frontend/pp/pp.h"
#include "common/arena.h"

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
	D_OTHER, D_NULL, D_IF, D_IFDEF, D_IFNDEF, D_ELIF, D_ELIFDEF, D_ELIFNDEF,
	D_ELSE, D_ENDIF, D_DEFINE, D_UNDEF, D_INCLUDE, D_INCLUDE_NEXT, D_IMPORT,
	D_PRAGMA, D_ERROR, D_WARNING, D_LINE, D_LINEMARK, D_IDENT, D_IMACROS, D_EMBED,
};

typedef struct pdir {
	uint32_t tok;	/* the '#' */
	uint32_t end;	/* first token after the line */
	uint32_t next;	/* conditionals: next #elif/#else/#endif of the group */
	uint32_t kind;
} pdir;

typedef struct pmacro pmacro;
typedef struct incent incent;

typedef struct pfile {
	const pdir *dir;
	_Atomic(const pmacro *) *mac;	/* per directive: the #define's macro, built once */
	_Atomic(const incent *) *inc;	/* per directive: #include "x" / <x>, its last answer */
	uint32_t ndir;
	uint32_t guard;		/* controlling macro id, 0 if none */
	int bad;		/* unbalanced conditionals */
	uint32_t badtok;	/* ... the directive name's token */
	uint32_t *lines;	/* offsets of '\n', built on first __LINE__ (atomic) */
	uint32_t nlines;
} pfile;

/* ---------------- arenas ---------------- */

/* ---------------- macros ---------------- */

enum { MK_OBJ, MK_FUNC, MK_BUILTIN };
enum {
	MF_VARIADIC = 1, MF_PASTE = 2, MF_VAOPT = 4,
	MF_BADHASH = 8,		/* '#' not before a parameter: an error unless assembler */
	MF_HASH = 16,		/* a '#' in the body */
	MF_UNKNOWN = 32,	/* an unknown token in the body (-Winvalid-pp-token) */
};
enum {
	B_NONE, B_FILE, B_LINE, B_COUNTER, B_DATE, B_TIME, B_TIMESTAMP,
	B_BASE_FILE, B_FILE_NAME, B_INCLUDE_LEVEL, B_PRAGMA,
	B_HAS_ATTRIBUTE, B_HAS_BUILTIN, B_HAS_FEATURE, B_HAS_EXTENSION,
	B_HAS_C_ATTRIBUTE, B_HAS_CPP_ATTRIBUTE, B_HAS_DECLSPEC, B_HAS_WARNING,
	B_HAS_CONSTEXPR_BUILTIN, B_IS_TARGET_ARCH, B_IS_TARGET_OS, B_IS_TARGET_VENDOR,
	B_IS_TARGET_ENVIRONMENT, B_IS_TARGET_VARIANT_OS, B_IS_TARGET_VARIANT_ENVIRONMENT,
	B_BUILDING_MODULE,
	B_IS_IDENTIFIER, B_HAS_INCLUDE, B_HAS_INCLUDE_NEXT, B_HAS_EMBED,
};

/*
 * Macro definitions are immutable and shared by all TUs, so that "the same
 * definition" is one pointer (the header cache compares macro table entries
 * by pointer): a #define in a header is built once per directive site
 * (pfile.mac), one in the main file or <built-in> is made canonical by its
 * content (canon_macro), and the builtin macros are process-wide.
 */
struct pmacro {
	const vtok *body;
	const vtok *plist;	/* function-like: "(" ... ")" (plist[0, nplist)), for -dM */
	const char *src;
	const uint8_t *pidx;	/* per body token: parameter index + 1, or 0 */
	uint32_t nbody, name;
	uint32_t hash;		/* canon_macro */
	uint16_t nparam, nplist;
	uint8_t kind, flags, builtin;
};

#define MPAGE_BITS 11
#define MPAGE (1u << MPAGE_BITS)
#define NMDIR ((1u << VINTERN_ID_BITS) >> MPAGE_BITS)

/* ---------------- contexts ---------------- */

enum { C_FILE, C_VTOK, C_PTOK, C_REPLAY };

typedef struct ctx {
	uint8_t type;
	uint8_t barrier;	/* yields T_EOF when exhausted instead of popping */
	uint8_t first;		/* 0x80 | SOL/SPACE for the first token (macro contexts) */
	uint8_t offloc;		/* C_VTOK: loc = loc + offset (a directive line) */
	uint8_t trail;		/* C_PTOK: ended in an empty argument after a space: the next token gets one */
	uint32_t i, n;
	uint32_t loc;		/* C_FILE: file base; C_VTOK: expansion loc */
	uint32_t mid;		/* macro to re-enable when popped */
	const vtok *vt;
	const ptok *pt;
	const struct htok *ht;	/* C_REPLAY: the entry's own tokens (pt: its escapes) */
	const char *src;	/* (C_REPLAY: the shared region's base) */
	/* C_FILE */
	const vlex_file *f;
	const pfile *fi;
	uint32_t di;		/* next directive */
	int32_t sdir;		/* search dir it was found in, -1 if none */
	uint32_t fe;		/* fent index */
	const char *spath;	/* the path as clang spells it (f->path is the cache key) */
	uint32_t sdl;		/* length of spath's directory part, '/' included */
	/* C_REPLAY: a header cache entry's tokens (pt, n), its children spliced in */
	const struct hent *he;
	uint32_t si, ci;	/* next segment, next __COUNTER__ record */
	unsigned cbase;		/* __COUNTER__ at the entry's start */
	uint8_t top;		/* outermost: owes the entry's pend_out when done (2: a top-level region) */
	uint8_t norec;		/* replayed while recording: its token positions are reserved, not copied */
} ctx;

typedef struct fent {
	uint32_t base;
	const vlex_file *f;
	const pfile *fi;
	const char *name;	/* for __FILE__ (prefix map applied) */
	uint32_t namelen;
	const char *spath;	/* as spelled, for dependency files */
	uint8_t sys;		/* a system header (found in a system directory, or included by one) */
	uint32_t from;		/* the #include's location (0: the main file, <built-in>, or a cached replay) */
	uint32_t dstate;	/* the #pragma diagnostic state where it was entered (pdnode index) */
	const char *cpos;	/* -C: its comments before here are printed or dropped (NULL: none yet) */
} fent;

#define MAXCTX (1u << 15)
#define SVMAX (1u << 24)

/* ---------------- header cache: types ---------------- */

/*
 * The header cache (step 2.4; -E text only for now).  Every #include is a
 * region.  While a region runs for the first time its inputs are recorded
 * exactly:
 *
 *   - every macro table entry it reads before writing it: the value at
 *     region entry, or only whether it was defined (#ifdef, defined(),
 *     include guards).  Definitions are shared pointers (see pmacro), so
 *     "same definition" is pointer equality;
 *   - #pragma once membership of the files it would enter;
 *   - names pasted from __COUNTER__ that it looks up (must be undefined);
 *
 * and its effects: the final value of every macro it wrote, the files it
 * marked once, the files it entered (dependencies, locations), the counter
 * delta, and its output text.  Uses of __COUNTER__ are relative: digit runs
 * are tracked through paste and stringify (ptok.ctr) and rewritten on
 * replay.  A region's text is a rope: its own text with the entries of the
 * nested regions it contains spliced in, so each header's text is stored
 * once however deep it is included.
 *
 * Regions nest; recording keeps one level per open region.  A read is
 * recorded in the innermost level; a level's records are merged into its
 * parent when it ends (and a replayed entry's into the level it is
 * replayed in).  A later TU with the same search path signature reaching
 * the same #include (file, search dir, spelling, nesting level, system
 * header or not) replays an entry whose inputs all match the current state
 * instead of running the header.  Regions that print diagnostics, change
 * #line state, use push/pop_macro, __DATE__/__TIME__/__TIMESTAMP__/
 * __BASE_FILE__ or __COUNTER__ in a directive are not cached.  Entries are
 * immutable and shared by all threads.
 */
#define HDEF ((uintptr_t)1)	/* read: only "defined" was read, and it was */
#define HNONE 0xffffffffu
#define HC_MAXLEV 64

typedef struct hread {
	uint32_t id, _pad;
	uintptr_t v;		/* value at entry (enabled), or HDEF */
} hread;

typedef struct honce {
	const vlex_file *f;
	uint32_t present;
} honce;

typedef struct hfent {
	const vlex_file *f;
	const pfile *fi;
	const char *name, *spath;
	uint32_t namelen, rbase;
	uint8_t sys;
} hfent;

/* __COUNTER__ digits: in a token's spelling (cprov) or in an entry's own text (hctr) */
typedef struct cprov {
	uint32_t val;
	uint32_t off;
	uint16_t len, next;	/* next run in the same token (a chain, increasing off) */
} cprov;

typedef struct hctr {
	uint32_t off, len, rel;	/* own-text offset; value - counter at entry */
} hctr;	/* tokens: off = own-token index, len = byte offset << 16 | length */

/* a lookup of an identifier pasted from __COUNTER__ (it was undefined) */
typedef struct hcread {
	const char *s;
	uint32_t len, off, dlen, rel;
} hcread;

typedef struct hent hent;

typedef struct hkid {
	const hent *e;
	const uint32_t *ex;
	uint32_t nex;
} hkid;

/*
 * A stored token (12 bytes; ptok is 24).  T_IDENT: sp is the id and the
 * spelling the id's.  Others: sp - 1 is the spelling's offset / 16 in the
 * shared region (hc_blob: stored once).  HF_X: sp indexes the entry's
 * escapes (full ptoks: spliced identifiers, long spellings, odd ids).
 */
typedef struct htok {
	uint32_t sp;
	uint32_t loc;		/* offset from the base + 1, 0: none */
	uint8_t kind, flags;
	uint16_t len;
} htok;
#define HF_X 128

static inline void htok_get(ptok *t, const htok *h, const ptok *xtk, const char *sbase, uint32_t base)
{
	if (__builtin_expect(h->flags & HF_X, 0)) {
		*t = xtk[h->sp];
	} else {
		t->kind = h->kind;
		t->flags = h->flags;
		t->len = h->len;
		t->ctr = 0;
		if (h->kind == T_IDENT) {
			t->id = h->sp;
			t->s = vintern_ents[h->sp].s;
		} else {
			t->id = 0;
			t->s = sbase + ((size_t)(h->sp - 1) << 4);
		}
	}
	t->loc = h->loc ? base + h->loc - 1 : 0;
}

typedef struct hseg {
	uint32_t at;		/* own-text offset (tokens: own-token index) the child goes before */
	uint32_t cdelta;	/* child's counter at entry - ours */
	const hent *child;
	uint32_t lbase;		/* tokens: child's location base - ours */
} hseg;

/*
 * Arrays of reads and writes are stored as chunks deduplicated across all
 * entries (an enclosing header's arrays repeat its children's runs).  The
 * cuts are content defined (by id), so the same run of ids cuts the same
 * way wherever it occurs.
 */
typedef struct hck {
	uint64_t h;
	uint32_t n, esz;	/* elements, element size (1: a blob of n bytes) */
	_Alignas(16) char d[];
} hck;

typedef struct hvec {
	const hck *const *c;
	uint32_t nc, n;		/* chunks, elements */
} hvec;

/* for (const T *x ...) over the elements of v; break leaves only the inner loop */
#define HV_FOR(v, T, x) \
	for (uint32_t hvi_ = 0; hvi_ < (v).nc; hvi_++) \
		for (const T *x = (const T *)(const void *)(v).c[hvi_]->d, *x##_e = x + (v).c[hvi_]->n; x < x##_e; x++)

/*
 * An include lookup an entry made itself that depended on the search
 * directories (used > 0).  The entry's ndirs/psig say the directories it
 * depended on were the same; where they differ, the entry still holds when
 * each such lookup gives the same answer here (hc_looks_ok): PostgreSQL
 * puts a per-target directory first, which every lookup passes through.
 */
typedef struct hlook {
	const char *name;	/* as written */
	const char *from;	/* the includer's spath (NULL: no includer) */
	const char *path;	/* the answer's spelling (NULL: not found) */
	const vlex_file *f;
	int32_t from_sdir, sdir;
	uint32_t nlen, from_sdl;
	uint8_t angled, next, from_bi, sys;
} hlook;

struct hent {
	hent *next;
	hvec rg;		/* inputs: include guards (v: HDEF or 0); checked first */
	hvec ru;		/* inputs: ids read while undefined (uint32_t) */
	hvec rv;		/* inputs: other reads */
	hvec dl;		/* delta: (id, new value) */
	const honce *ord;
	const vlex_file *const *oadd;
	const hfent *fe;
	const char *text;	/* own text */
	const htok *tk;		/* token entries: own tokens */
	const ptok *xtk;	/* their escapes (HF_X) */
	const hctr *ctr;
	const hcread *crd;
	const hseg *seg;
	/*
	 * Child entries in order: their reads are this entry's inputs except
	 * the ids this entry wrote before the child (ex, sorted), and their
	 * deltas apply before this entry's own (dl: the ids whose last write was
	 * this entry's own).  Reads and writes are stored once, in the entry
	 * that made them.
	 */
	const struct hkid *kid;
	uint32_t nkid;
	const hlook *lk;	/* its own lookups that depended on the search directories */
	uint32_t nlk;
	size_t ntext, total;	/* own text (tokens); with the children */
	int32_t level;		/* __INCLUDE_LEVEL__ used: the include depth it ran at (-1: not used) */
	uint32_t ndirs, psig;	/* leading search directories its lookups depended on; their id */
	const uint32_t *br;	/* token entries: outermost '{' '}' pairs of the own tokens with no child
				   inside, (open, close) own-token indices, by open (pp_lazy_skip) */
	uint32_t nbr;
	uint32_t nord, noadd, nfe, nctr, ncrd, nseg, span, counter_delta;
	uint8_t has_text, first_sol, col_end, pend_out;
	uint8_t edir_end;
	uint8_t pk, pk_L, pk_pp;	/* the printer's last token at the end (pp.pk...) */
};

typedef struct hkey {
	uint64_t hash;
	const vlex_file *f;
	const char *spath;
	uint32_t sig, nfile;
	int32_t sdir;
	uint8_t sys;
	_Atomic(hent *) head;
	_Atomic uint32_t n;
} hkey;

/* recording, per TU */
enum { HT_R = 1, HT_W = 2, HT_G = 4 /* the read was an include guard check */,
       HT_O = 8 /* the last write was the level's own (not a child entry's) */ };

typedef struct htouch {	/* a level's first touch of an id */
	uint32_t id, flags;	/* HT_* */
	uintptr_t v;		/* HT_R: value at entry or HDEF */
	uint64_t prev;		/* the id's stamp before (the enclosing levels') */
} htouch;

typedef struct hcpos {		/* __COUNTER__ digits in the output */
	size_t pos;
	uint32_t len, val;
} hcpos;

typedef struct hcrd {		/* hcread while recording: absolute value */
	const char *s;
	uint32_t len, off, dlen, val;
} hcrd;

typedef struct hsegr {		/* a child entry's text (tokens) in the output */
	size_t start, end;
	const hent *child;
	uint32_t counter0;
	uint32_t base0;		/* tokens: its location base */
} hsegr;

typedef struct hlev {
	hkey *k;
	uint32_t serial;
	int depth, errors0, warns0, text_started;
	uint32_t t0, o0, c0, r0, s0;	/* starts in the stacks below */
	uint32_t fe0, base0, nonce0, nlm0, npushed0;
	unsigned counter0;
	size_t text0;
	uint8_t first_sol, uses_level;
	uint32_t nfile, ndirs;
	uint32_t k0, x0;	/* starts in rec.kid, rec.kex */
	uint32_t l0;		/* start in rec.lk */
} hlev;

typedef struct hkidr {		/* a child entry of a level while recording */
	const hent *e;
	uint32_t ex0, nex;	/* its exclusions in rec.kex */
} hkidr;

/* -pedantic warnings on hot paths (__COUNTER__, every #define of a variadic macro): their
   command-line state, once per TU, so the common case (off, no pragmas) is one test */
enum { PPW_C2Y, PPW_VARIADIC, PPW_N };

struct pp {
	uintptr_t *mdir[NMDIR];
	const pp_opts *o;
	ctx *cx;
	int depth, nfile;
	varena perm, tmp;
	bool hold_tmp;		/* the consumer holds earlier tokens: tmp is not reset (pp_hold_tmp) */
	ptok *sv;		/* scratch token stack */
	size_t svn;
	fent *fe;
	uint32_t nfe, next_base;
	uint64_t ntok_out;	/* tokens pp_next delivered */
	uint64_t skipped_tok;	/* tokens of regions pp_skip_top dropped */
	pp_hitreg *drh;		/* VCC_DRCOV: header-cache hits in delivered positions (pp_hit_regions) */
	uint32_t ndrh, ndrs;
	struct { uint64_t pos; int depth; const void *he; uint32_t base; unsigned ctr; } drs[64];	/* hits whose replay is still open */
	pp_topev *tev;		/* top-level region events (pp_top_events) */
	uint32_t ntev;
	uint8_t tev_full, top_rec;
	const void *hc_last;	/* the entry hc_apply applied last */
	uint8_t pend;		/* SOL/SPACE owed to the next token (empty expansions) */
	uint8_t rep_norec;	/* the token raw() just gave came from a norec replay */
	int preexp;		/* in argument pre-expansion */
	int errors;
	unsigned counter;
	const vlex_file **once;
	uint32_t nonce, oncecap;
	struct { uint32_t id; uintptr_t m; } *pushed;
	uint32_t npushed;
	/* #pragma clang/GCC diagnostic: states are a persistent list (node: one flag's mapping and
	   the state it was set in, 0: the command line's); the transitions are by location */
	struct pdnode { uint32_t prev; uint8_t act; char name[59]; } *dn;
	struct { uint32_t loc, st; } *dtr;
	uint32_t ndn, ndtr, dcur, ndstk, dstk[64];
	uint32_t nden;			/* "warning"/"error" nodes: only they turn a warning on */
	struct mdef *mdx;		/* every #define's site, by name (built for a macro note) */
	uint32_t nmdx, cmdx, mdx_nfe;
	const void *wc_o;		/* the options wc[] was computed for */
	bool wc[PPW_N];			/* -pedantic warnings on hot paths: on (command line) */
	vlex_file builtin;
	const char *base_name;
	uint32_t base_namelen;
	jmp_buf *fatal_jb;	/* fatal errors end the TU here */
	int color;		/* diagnostics in colour */
	uint32_t diag_last;	/* the last diagnostic's location + 1, and whether it was a note */
	bool diag_lastnote;
	uint32_t diag_fe;	/* the file of the last diagnostic + 1 (for "In file included from") */
	vdiag *dhead, *dtail;	/* the TU's diagnostic records (in perm) */
	bool dtrack;		/* VCC_TRACK_DIAG: vcc's call site after each diagnostic */
	bool dfinished;		/* pp_diag_finish ran (JSON flushed, SARIF written) */
	vdiag *dpend;		/* -fdiagnostics-format=json: the record not yet written */
	vdiag *tpend;		/* vcc's text style: the record not yet rendered (its notes may follow) */
	uint32_t dlast1;	/* clang's LastLoc + 1 (0: none) and LastLevel: the previous record's */
	uint8_t dlastlv;
	struct lmark {		/* #line / GNU line markers */
		uint32_t fe, off;	/* in fent fe, from byte offset off on */
		uint32_t pline, line;	/* physical line pline is line */
		const char *name;	/* presumed file name, or NULL */
		uint32_t namelen;
	} *lm;
	uint32_t nlm;
	int warns;
	int in_dir;		/* evaluating a directive's tokens (#if, #include, #line) */
	int collecting;		/* in collect(): directives are inside macro arguments */
	uint32_t coll_loc, coll_name;	/* the outermost macro collecting: its name token, id */
	/* -E text (pp_text) */
	char *out;
	size_t nout, capout;
	unsigned col;
	/* the last token printed, for clang's AvoidConcat: kind, an identifier "L", '.' before it, its end */
	uint8_t pk, pk_L, pk_pp;
	const char *pk_end;
	uint8_t sol_on, edir, cmt_sol;	/* cmt_sol: -C, an empty expansion began the line: a comment next starts it */	/* edir: a directive was printed on this line (clang's EmittedDirectiveOnThisLine) */
	uint32_t cur_line;
	uint8_t emb_sp, emb_tail;	/* -E after #embed: pending, tokens were suffix()/if_empty() */		/* -E: the previous token was #embed's last byte */
	uint8_t out_text;	/* pp_text: track lines for the printer */	/* clang's CurLine: the presumed line the output is on */
	int sol_depth;
	uint32_t exp_loc;
	uint32_t sol_loc;	/* the source macro name a line starts with: expanded tokens' column */
	/* -E without -P: GNU line markers */
	uint8_t lmark, lm_blk, lm_sys;	/* on; <built-in>'s block printed; the current file is a system header */
	uint32_t lm_n, lm_bline;	/* predefined macros before the command line (+1); <built-in>'s line */
	const char *lm_name;		/* the current file's presumed name */
	uint32_t lm_nlen;
	struct { uint32_t id; const pmacro *m; } *dd_ev;	/* -dD: <built-in>'s #define/#undef, until printed */
	uint32_t ndd, capdd;
	int dd_done;
	/* header cache */
	int hc;			/* on (pp_text or pp_next, not disabled) */
	int tokmode;		/* 0 unset, 1 text (pp_text), 2 tokens (pp_next) */
	uint32_t sig;		/* base signature (HNONE: no cache) */
	uint32_t *psig;		/* search path prefix ids, [0, ndirs] */
	struct hrec {
		int on;		/* n > 0 */
		int n, nbad;	/* levels; levels [0, nbad) are not cacheable */
		int badline;
		uint32_t serial;
		hlev lev[HC_MAXLEV];
		htouch *t;
		uint32_t nt;
		honce *ord;
		uint32_t nord;
		hcpos *ctr;
		uint32_t nctr;
		hcrd *crd;
		uint32_t ncrd;
		hsegr *seg;
		uint32_t nseg;
		hkidr *kid;	/* child entries, in order (their reads and writes are theirs) */
		uint32_t nkid;
		uint32_t *kex;
		uint32_t nkex;
		hlook *lk;	/* the levels' own lookups (hlook) */
		uint32_t nlk;
		cprov *cp;	/* indexed by ptok.ctr */
		uint32_t ncp;
		ptok *tk;	/* tokens handed out (token mode) */
		uint32_t ntk;
		char *sb;	/* their spellings that live in per-TU memory */
		size_t nsb;
	} rec;
	uint64_t *rst[NMDIR];	/* per id: serial << 32 | index in rec.t */
	struct { const void *e; uint32_t gen, ok; } hm[4096];	/* child entry verdicts in one hc_try */
	uint32_t hgen;
	size_t hc_bytes;	/* what the entry being published allocated */
	const void *bcache[4096];	/* recent spelling blobs, by hash */
	const char *kspell[256];	/* punctuator spelling blobs, by kind */
	const hck *ckc[4096];
	const hck *vkc[4096];	/* recent chunks, by (first id, last id, length) */	/* recent chunks, by hash (most repeats are a child's) */
};

#define error(p, loc, ...) pp_diag(p, loc, "error", __VA_ARGS__)
#define fatal(p, loc, ...) pp_diag(p, loc, "fatal error", __VA_ARGS__)
#define warn(p, loc, ...) pp_diag(p, loc, "warning", __VA_ARGS__)

/* ---------------- header cache: recording reads and writes ---------------- */

/* the open regions are not cacheable (something in them depends on more than we record) */
VPRIV void diag_pragma(pp *p, const char *q, const char *e, uint32_t loc);
VPRIV void diag_file_end(pp *p, uint32_t fe);
#define HC_BAD(p) do { if ((p)->rec.n) { (p)->rec.nbad = (p)->rec.n; (p)->rec.badline = __LINE__; } } while (0)

VPRIV void pp_wc_fill(pp *p);

/* false: warning k cannot be reported in this TU (off on the command line, no pragmas) */
static inline bool pp_wc_maybe(pp *p, int k)
{
	if (p->wc_o != p->o)
		pp_wc_fill(p);
	return p->wc[k] || p->nden;
}

#define HC_MAXT (1u << 24)

#define HC_MAXCP 65535u

#define HC_MAXCRD 65536u

/* ---------------- header cache: shared table ---------------- */

typedef struct {
	_Atomic unsigned long hit, miss, rec, uncacheable, full, bytes_replayed, bytes_rec, mem;
	_Atomic unsigned long nru, nrv, ndl, nfe, nvalid, nchecked;
	_Atomic unsigned long b_rd, b_dl, b_once, b_fe, b_text, b_seg, b_ent;	/* bytes stored, by kind */
} hstat_t;

/* ---------------- #if expressions ---------------- */

typedef struct ival {
	uint64_t v;
	bool u;
} ival;

/*
 * Include resolution, process-wide.  A lookup's answer depends only on the
 * options' signature (cwd, frameworks, ...), the mode (where the search
 * starts, angled, include_next), the includer's directory (quoted), the name
 * and the search directories it looked at: the ones up to where it found
 * the file (all if not found), whose prefix signature (psig) it keeps.
 * Files never change under the token cache (vlex_get caches misses too).
 * Kbuild puts the per-directory -I last, so one entry serves every TU.  A
 * warm lookup is one probe instead of a path build, directory-cache probe
 * and token-cache probe per search directory.
 *
 * The key also holds the whole search path's id (fsig): meson puts each
 * target's own -I first, so the prefix an answer depends on differs per
 * target anyway, and without fsig the entries of hundreds of targets for
 * the same header shared one hash and one probe cluster (PostgreSQL: 175
 * probes per lookup, half of them same-key entries for other targets).
 */
struct incent {
	uint64_t hash;
	uint32_t sig, fsig, klen;
	int32_t mode;		/* start * 4 + angled * 2 + next */
	uint32_t used, usig;	/* directories looked at, psig[used] */
	int32_t sdir;
	const vlex_file *f;
	const char *path;
	char key[];		/* includer's directory, '\0', name; zero padded to 8 */
};

/* ---------------- header cache: regions ---------------- */

#define HC_MAXVAR 64	/* entries per key */
#define HC_MAXORD (1u << 20)
#define HC_MAXSEG (1u << 20)

#define HC_MAXTK (1u << 24)
#define HC_MAXSB ((size_t)1 << 28)
#define HC_MAXKID (1u << 20)
#define HC_MAXKEX (1u << 22)
#define HC_MAXLK (1u << 16)

typedef struct {
	htouch *t;
	honce *ord;
	hcpos *ctr;
	hcrd *crd;
	hsegr *seg;
	hkidr *kid;
	uint32_t *kex;
	hlook *lk;
	cprov *cp;
} rec_spare_t;

/* ---------------- across files ---------------- */
/* next-token.c */
VPRIV void hc_mode(pp *p, int mode);

/* translation-unit.c */
VPRIV void *vm(size_t n);
static inline int utoa(char *b, unsigned long long v);
/* file.c */
extern _Atomic size_t shared_used;
VPRIV void *salloc(size_t n);
VPRIV const char *shared_base0(void);
VPRIV void *hsalloc(pp *p, size_t n);
VPRIV hvec hc_vec(pp *p, const void *a, uint32_t n, uint32_t esz);
VPRIV const void *hc_blob(pp *p, const void *s, size_t n);
VPRIV const pfile *pfile_get(const vlex_file *f, bool *fresh);
VPRIV uint32_t file_line(const vlex_file *f, const pfile *cfi, uint32_t off);
/* diag.c */
VPRIV uint32_t loc_line(const pp *p, uint32_t loc);
VPRIV bool move_to_line(pp *p, uint32_t line, bool req);
VPRIV void out_nl(pp *p);
VPRIV void cmt_flush(pp *p, ctx *c, const char *upto);
VPRIV __attribute__((cold)) void cmt_skip_to(pp *p, const ptok *rp);
VPRIV const char *cmt_line_end(const char *s);
VPRIV unsigned tok_col(const pp *p, uint32_t loc);
VPRIV void text_file_change(pp *p, const ctx *from, int enter);
VPRIV void lm_count(pp *p);
VPRIV void lm_enter(pp *p, const fent *e);
VPRIV void lm_rename(pp *p, const char *name, uint32_t nlen, uint32_t line);
VPRIV const char *presumed_name(const pp *p, uint32_t fe, uint32_t loc, uint32_t *len);
VPRIV __attribute__((format(printf, 4, 5))) void pp_diag(pp *p, uint32_t loc, const char *what, const char *fmt, ...);
void pp_diagx(pp *p, const pp_dinfo *d, const char *fmt, ...);
bool pp_warnx(pp *p, const char *flag, bool on, const char *const *groups, pp_dinfo *d, const char *fmt, ...);
VPRIV bool obj_macro_at(pp *p, uint32_t loc);
VPRIV void dir_extra_tok(pp *p, const ctx *c, uint32_t name, uint32_t i);
void pp_diag_summary(pp *p);
VPRIV const fent *loc_fent(const pp *p, uint32_t loc);
VPRIV bool loc_ok(const pp *p, uint32_t loc, const fent **e);
VPRIV int diag_where(const pp *p, uint32_t loc, const char **name, uint32_t *nl, uint32_t *line, uint32_t *col);
struct vbuf;
VPRIV void render_text(pp *p, struct vbuf *b, const vdiag *d);
VPRIV bool render_head(pp *p, struct vbuf *b, const vdiag *d, const fent **ep);
VPRIV void parseable_fixits(pp *p, struct vbuf *b, const vdiag *d);
VPRIV uint32_t dcols(const char *s, uint32_t b, uint32_t e, uint32_t col);
VPRIV void db_c(struct vbuf *b, char c, int n);
VPRIV void include_stack(pp *p, struct vbuf *b, const fent *e);
/* diagtext.c: vcc's text style (records rendered when the next one starts, so that
   their notes can fold into the snippet), the narrated style, the type diff */
VPRIV void diag_text_flush(pp *p);
VPRIV void diag_narrate(pp *p, struct vbuf *b, vdiag *d);
VPRIV void diag_render_rec(pp *p, struct vbuf *b, vdiag *d);
VPRIV void diag_render_color(pp *p, struct vbuf *b, vdiag *d);
VPRIV void diag_typediff(struct vbuf *b, const vdiag *d, const char *plain);
/* diagout.c: JSON lines and SARIF */
VPRIV void diag_json_flush(pp *p);
VPRIV void diag_fixits_out(pp *p);
/* include.c: dependencies */
VPRIV uint8_t inc_sys(pp *p, int32_t sdir);
VPRIV void dep_only(pp *p, const char *path, uint8_t sys);

/* macro-dump.c: -dD */
VPRIV void dd_out(pp *p, uint32_t id, const pmacro *m, bool builtin);
VPRIV void dd_flush(pp *p);

/* macro.c */
static inline uintptr_t mget(const pp *p, uint32_t id);
static inline uintptr_t *mslot(pp *p, uint32_t id);
static inline void mset(pp *p, uint32_t id, const pmacro *m);
static inline void mdisable(pp *p, uint32_t id);
static inline void menable(pp *p, uint32_t id);
/* hcrec.c */
VPRIV uint64_t *rslot(pp *p, uint32_t id);
static inline htouch *touch(pp *p, uint32_t id, bool *isnew);
static inline void touch_read(pp *p, uint32_t id, uintptr_t v, uint32_t g);
static inline void touch_write(pp *p, uint32_t id);
VPRIV __attribute__((noinline)) void rec_read(pp *p, uint32_t id, uintptr_t e, int def);
VPRIV uint16_t new_cprov(pp *p, uint32_t val, uint32_t off, uint32_t len, uint16_t next);
VPRIV uint16_t cp_chain(pp *p, uint16_t a, uint32_t d, uint16_t tail);
VPRIV __attribute__((noinline)) void rec_ctr_read(pp *p, const ptok *t, uintptr_t e);
static inline uintptr_t mread_tok(pp *p, const ptok *t, int def);
static inline uintptr_t mread(pp *p, uint32_t id);
static inline bool mdefined(pp *p, uint32_t id);
static inline bool mguard(pp *p, uint32_t id);
VPRIV void mdef(pp *p, uint32_t id, const pmacro *m);
/* hcache.c */
extern hstat_t hstat;
extern size_t hc_mem_max;
VPRIV bool sig_of(const pp_opts *o, uint32_t *sig, uint32_t *psig);
VPRIV hkey *hkey_get(uint32_t sig, const vlex_file *f, int32_t sdir, uint32_t nfile, const char *spath, uint8_t sys, bool make);
/* text.c */
extern _Thread_local char *out_spare;
extern _Thread_local size_t out_spare_cap;
VPRIV void out_reserve(pp *p, size_t n);
static inline void out_put(pp *p, const char *s, size_t n);
static inline void out_ch(pp *p, char ch);
/* token.c */
static inline void vfill(ptok *t, const vtok *v, const char *src, uint32_t loc);
const char *pp_spell(const ptok *t, char *buf, size_t *len);
VPRIV char *spell_to(char *dst, const ptok *t);
static inline char *spell_to16(char *dst, const ptok *t);
static inline void svpush(pp *p, const ptok *t);
VPRIV ptok *svtake(pp *p, size_t base, uint32_t *n);
static inline ctx *push(pp *p);
VPRIV void push_ptok(pp *p, const ptok *t, uint32_t n, uint32_t mid, uint8_t first, int barrier);
VPRIV void push_line(pp *p, const ctx *fc, uint32_t b, uint32_t e);
VPRIV void pop_to(pp *p, int d);
#define DRCOV_MAX 65536
extern int pp_drcov;
VPRIV void drcov_hit(pp *p, int d0);
VPRIV void drcov_end(pp *p);
VPRIV void top_event(pp *p, const void *e, uint32_t base, unsigned ctr, uint8_t end, uint8_t hit);
VPRIV __attribute__((noinline)) void raw(pp *p, ptok *t);
static inline __attribute__((always_inline)) bool raw_plain(pp *p, ctx *c, ptok *t);
VPRIV __attribute__((noinline)) void raw_pop(pp *p, ptok *t);
static inline __attribute__((always_inline)) void raw_fast(pp *p, ptok *t);
VPRIV __attribute__((noinline)) void next_exp_slow(pp *p, ptok *t, uintptr_t e);
static inline __attribute__((always_inline)) void next_exp(pp *p, ptok *t);
/* expand.c */
VPRIV void macro_note_(pp *p, const pmacro *m, uint32_t before, const char *msg);
VPRIV bool is_strlit(int k);
extern const uint8_t idch_tab[256];
static inline __attribute__((always_inline)) void next_tok(pp *p, ptok *t)
{
	if (p->depth == p->nfile && !p->svn && !p->hold_tmp)
		p->tmp.p = p->tmp.base;	/* nothing above file level refers to tmp */
	next_exp(p, t);
}

static inline bool idch(unsigned char c);
VPRIV int expand(pp *p, const pmacro *m, ptok *t);
/* builtin.c */
VPRIV const ctx *cur_file(const pp *p);
VPRIV bool header_name(pp *p, char *buf, size_t cap, size_t *len, bool *angled, ptok *t);
VPRIV int builtin(pp *p, const pmacro *m, ptok *t);
/* ifexpr.c */
VPRIV ival parse_number(pp *p, const ptok *t);
VPRIV bool eval_if(pp *p, const ctx *fc, uint32_t b, uint32_t e);
VPRIV bool eval_ptoks(pp *p, const ptok *t, uint32_t n, uint64_t *v);
VPRIV void embed(pp *p, ctx *c, uint32_t b, uint32_t e, uint32_t loc, uint32_t nloc);
VPRIV int has_embed(pp *p, const ctx *fc, uint32_t loc);
/* directive.c */
VPRIV const char *parse_define(const vtok *vt, uint32_t b, uint32_t e, pmacro *m, void *(*mem)(void *, size_t), void *mc, uint32_t *eoff);
VPRIV void *mem_perm(void *a, size_t n);
VPRIV void *mem_shared(void *a, size_t n);
/* macro.c */
extern const vtok no_body[1];
VPRIV void define(pp *p, ctx *c, uint32_t b, uint32_t e);
/* include.c */
VPRIV const vlex_file *try_path(const pp_opts *o, const char *path, size_t len);
VPRIV const vlex_file *find_include(pp *p, const ctx *from, const char *name, size_t nlen, bool angled, bool next, int32_t *sdir, const char **path, const incent **ie);
VPRIV bool hc_looks_ok(pp *p, const hent *e);
/* hcache.c */
VPRIV int once_index(const pp *p, const vlex_file *f);
VPRIV void once_add(pp *p, const vlex_file *f);
VPRIV bool hc_top(const pp *p);
VPRIV bool hc_valid(pp *p, const hent *e);
VPRIV void emit(pp *p, const hent *e, unsigned base);
static inline void text_start(pp *p, uint8_t sol);
/* hcentry.c */
VPRIV bool replay_seg(pp *p, ctx *c);
VPRIV void replay_tok(pp *p, ctx *c, ptok *t);
VPRIV void rec_tok(pp *p, const ptok *t);
VPRIV bool hc_try(pp *p, hkey *k);
extern _Thread_local ptok *tk_spare;
extern _Thread_local char *sb_spare;
extern _Thread_local rec_spare_t rec_spare;
VPRIV void hc_begin(pp *p, hkey *k);
VPRIV void hc_end(pp *p);
/* include.c */
VPRIV void enter_file(pp *p, const vlex_file *f, const char *path, int32_t sdir);
VPRIV void include(pp *p, ctx *c, uint32_t b, uint32_t e, int kind, uint32_t loc);
/* directive.c */
VPRIV int pragma_text(pp *p, const char *s, size_t n, uint32_t loc, ptok *out, uint32_t sloc);
VPRIV int directive(pp *p, ctx *c, ptok *out);
VPRIV bool pp_pad_at(const char *s, uint32_t ls, uint32_t le, uint32_t x);

/* decimal digits of v at b (no terminator); returns the length */
static inline int utoa(char *b, unsigned long long v)
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

static inline uintptr_t mget(const pp *p, uint32_t id)
{
	const uintptr_t *pg = p->mdir[id >> MPAGE_BITS];

	return pg ? pg[id & (MPAGE - 1)] : 0;
}

static inline uintptr_t *mslot(pp *p, uint32_t id)
{
	uintptr_t **pg = &p->mdir[id >> MPAGE_BITS];

	if (unlikely(!*pg)) {
		*pg = varena_alloc(&p->perm, MPAGE * sizeof(uintptr_t));
		memset(*pg, 0, MPAGE * sizeof(uintptr_t));
	}
	return &(*pg)[id & (MPAGE - 1)];
}

static inline void mset(pp *p, uint32_t id, const pmacro *m)
{
	*mslot(p, id) = (uintptr_t)m;
}

/* id is a macro being expanded: its page exists (it was read, defined) */
static inline void mdisable(pp *p, uint32_t id) { p->mdir[id >> MPAGE_BITS][id & (MPAGE - 1)] |= 1; }
static inline void menable(pp *p, uint32_t id) { p->mdir[id >> MPAGE_BITS][id & (MPAGE - 1)] &= ~(uintptr_t)1; }

/* the innermost level's touch entry for id (new if *isnew) */
static inline htouch *touch(pp *p, uint32_t id, bool *isnew)
{
	uint64_t *s = rslot(p, id);
	uint32_t ser = p->rec.lev[p->rec.n - 1].serial;

	if ((uint32_t)(*s >> 32) == ser) {
		*isnew = false;
		return &p->rec.t[(uint32_t)*s];
	}
	if (unlikely(p->rec.nt == HC_MAXT)) {
		HC_BAD(p);
		*isnew = false;
		return NULL;
	}
	*isnew = true;
	p->rec.t[p->rec.nt] = (htouch){ id, 0, 0, *s };
	*s = (uint64_t)ser << 32 | p->rec.nt;
	return &p->rec.t[p->rec.nt++];
}

/* id read with value v at entry of the innermost level (v: enabled, or HDEF) */
static inline void touch_read(pp *p, uint32_t id, uintptr_t v, uint32_t g)
{
	bool isnew;
	htouch *t = touch(p, id, &isnew);

	if (!t)
		return;
	if (isnew) {
		t->flags = HT_R | g;
		t->v = v;
	} else if (v != HDEF && (t->flags & HT_R) && t->v == HDEF && !(t->flags & HT_W)) {
		t->v = v;	/* defined-only so far; unwritten, so this is the entry value */
	}
}

static inline void touch_write(pp *p, uint32_t id)
{
	bool isnew;
	htouch *t = touch(p, id, &isnew);

	if (t)
		t->flags |= HT_W | HT_O;
}

/* a child entry wrote id: not the level's own write any more */
static inline void touch_write_kid(pp *p, uint32_t id)
{
	bool isnew;
	htouch *t = touch(p, id, &isnew);

	if (t)
		t->flags = (t->flags | HT_W) & ~(uint32_t)HT_O;
}

/* the entry for token t's identifier, as an expansion reads it */
static inline uintptr_t mread_tok(pp *p, const ptok *t, int def)
{
	uintptr_t e = mget(p, t->id);

	if (unlikely(p->rec.on)) {
		if (t->ctr)
			rec_ctr_read(p, t, e);
		else
			rec_read(p, t->id, e, def);
	}
	return e;
}

/* the entry for id, as a macro expansion reads it */
static inline uintptr_t mread(pp *p, uint32_t id)
{
	uintptr_t e = mget(p, id);

	if (unlikely(p->rec.on))
		rec_read(p, id, e, 0);
	return e;
}

/* is id defined (#ifdef, defined(), include guards) */
static inline bool mdefined(pp *p, uint32_t id)
{
	uintptr_t e = mget(p, id);

	if (unlikely(p->rec.on))
		rec_read(p, id, e, 1);
	return e != 0;
}

/* an include guard: defined? */
static inline bool mguard(pp *p, uint32_t id)
{
	uintptr_t e = mget(p, id);

	if (unlikely(p->rec.on))
		rec_read(p, id, e, 2);
	return e != 0;
}

static inline void out_put(pp *p, const char *s, size_t n)
{
	out_reserve(p, n);
	memcpy(p->out + p->nout, s, n);
	p->nout += n;
}

static inline void out_ch(pp *p, char ch)
{
	out_reserve(p, 1);
	p->out[p->nout++] = ch;
}

/* three stores: s, len|id, loc|kind|flags|ctr (little endian) */
static inline void vfill(ptok *t, const vtok *v, const char *src, uint32_t loc)
{
	uint64_t li, lk;
	uint32_t kf;

	memcpy(&li, &v->len, 8);
	memcpy(&kf, &v->kind, 4);
	kf &= 0xffu | (uint32_t)(TF_SOL | TF_SPACE | TF_DIRTY | TF_RAW) << 8;	/* ctr = 0 */
	lk = loc | (uint64_t)kf << 32;
	t->s = src + v->off;
	memcpy(&t->len, &li, 8);
	memcpy(&t->loc, &lk, 8);
}

/*
 * spell_to() where dst has 16 bytes of room past the spelling: a short clean
 * spelling is copied as 16 bytes (spellings are readable 16 bytes past their
 * end: file text is padded, and so is every spelling pp makes)
 */
static inline char *spell_to16(char *dst, const ptok *t)
{
	if (likely(!(t->flags & TF_DIRTY) && t->len <= 16)) {
		memcpy(dst, t->s, 16);
		return dst + t->len;
	}
	return spell_to(dst, t);
}

static inline void svpush(pp *p, const ptok *t)
{
	if (unlikely(p->svn == SVMAX))
		fatal(p, 0, "macro expansion too large");
	p->sv[p->svn++] = *t;
}

static inline ctx *push(pp *p)
{
	ctx *c;

	if (unlikely(p->depth == MAXCTX))
		fatal(p, 0, "macro expansion or #include nesting too deep");
	c = &p->cx[p->depth++];
	memset(c, 0, offsetof(ctx, f));
	return c;
}

/* the next token of the top context if it is plain (nothing owed, no directive) */
static inline __attribute__((always_inline)) bool raw_plain(pp *p, ctx *c, ptok *t)
{
	if (likely(c->i < c->n && !(c->first | p->pend))) {
		if (likely(c->type == C_PTOK)) {
			*t = c->pt[c->i++];
			return true;
		}
		if (c->type == C_VTOK) {
			const vtok *v = &c->vt[c->i++];

			vfill(t, v, c->src, c->offloc ? c->loc + v->off : c->loc);
			return true;
		}
		if (c->type == C_FILE) {
			const vtok *v = &c->vt[c->i];

			if (likely(!((v->flags & TF_SOL) && v->kind == T_HASH))) {
				c->i++;
				vfill(t, v, c->src, c->loc + v->off);
				return true;
			}
		}
	}
	return false;
}

/* raw(), inline: a macro context's next token with nothing owed (most tokens) */
static inline __attribute__((always_inline)) void raw_fast(pp *p, ptok *t)
{
	if (likely(raw_plain(p, &p->cx[p->depth - 1], t)))
		return;
	raw_pop(p, t);
}

/* the next fully expanded token */
/*
 * assembler-with-cpp: '$' is no identifier character (clang), so in "$APIC_ID"
 * the macro APIC_ID expands: t becomes the '$' and the name is read next.
 * (vcc's lexer is shared with C, where '$' is one: the split is made here.)
 */
static __attribute__((noinline, cold)) void dollar_split(pp *p, ptok *t)
{
	uint32_t id;

	if (t->len < 2 || t->s[0] != '$' || (t->flags & TF_DIRTY) || t->s[1] == '$' || (t->s[1] >= '0' && t->s[1] <= '9'))
		return;
	id = vintern_find(t->s + 1, t->len - 1, vintern_hash(t->s + 1, t->len - 1));
	if (!id || !mget(p, id))
		return;
	{
		ptok *n = varena_alloc(&p->tmp, sizeof(*n));

		*n = *t;
		n->s = t->s + 1, n->len = t->len - 1, n->id = id, n->loc = t->loc + 1;
		n->flags &= (uint8_t)~(TF_SOL | TF_SPACE);
		push_ptok(p, n, 1, 0, 0, 0);
		t->len = 1, t->id = 0, t->kind = T_UNKNOWN;
	}
}

static inline __attribute__((always_inline)) void next_exp(pp *p, ptok *t)
{
	uintptr_t e;

	raw_fast(p, t);
	if (t->kind != T_IDENT || (t->flags & PF_NOEXP))
		return;
	e = mread_tok(p, t, 0);
	if (likely(!e)) {
		if (unlikely(p->o->asm_mode) && t->s[0] == '$')
			dollar_split(p, t);
		return;
	}
	next_exp_slow(p, t, e);
}

static inline bool idch(unsigned char c)
{
	return idch_tab[c];
}

/* the first output of the open levels that have none yet starts here */
static inline void text_start(pp *p, uint8_t sol)
{
	for (int i = p->rec.n - 1; i >= 0 && !p->rec.lev[i].text_started; i--) {
		p->rec.lev[i].text_started = 1;
		p->rec.lev[i].first_sol = sol;
		p->rec.lev[i].text0 = p->nout;
	}
}

#endif
