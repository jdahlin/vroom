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

#include "dircache.h"
#include "intern.h"
#include "pp.h"
#include "ppdata.inc"

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)

static void *vm(size_t n)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

	if (p == MAP_FAILED) {
		perror("vcc: mmap");
		abort();
	}
	return p;
}

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

/* ---------------- shared (process-wide) allocation ---------------- */

static char *shared_base;
static _Atomic size_t shared_used;
#define SHARED_MAX (1ull << 34)	/* address space; the header cache stops storing at hc_mem_max (8 GB) */

static void *salloc(size_t n)
{
	size_t off;

	if (unlikely(!__atomic_load_n(&shared_base, __ATOMIC_ACQUIRE))) {
		char *b = vm(SHARED_MAX), *z = NULL;

		if (!__atomic_compare_exchange_n(&shared_base, &z, b, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			munmap(b, SHARED_MAX);
	}
	off = atomic_fetch_add(&shared_used, (n + 15) & ~(size_t)15);
	if (off + n > SHARED_MAX)
		abort();
	return shared_base + off;
}

/* ---------------- per-file skeleton ---------------- */

enum {
	D_OTHER, D_NULL, D_IF, D_IFDEF, D_IFNDEF, D_ELIF, D_ELIFDEF, D_ELIFNDEF,
	D_ELSE, D_ENDIF, D_DEFINE, D_UNDEF, D_INCLUDE, D_INCLUDE_NEXT, D_IMPORT,
	D_PRAGMA, D_ERROR, D_WARNING, D_LINE, D_LINEMARK, D_IDENT,
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
	uint32_t *lines;	/* offsets of '\n', built on first __LINE__ (atomic) */
	uint32_t nlines;
} pfile;

static int spelled(const vlex_file *f, const vtok *v, const char *s)
{
	size_t n = strlen(s);

	return v->len == n && !memcmp(f->src + v->off, s, n);
}

static uint32_t dir_kind(const vlex_file *f, uint32_t i, uint32_t end)
{
	const vtok *v = &f->tok[i];

	if (i >= end)
		return D_NULL;
	if (v->kind == T_NUMBER)
		return D_LINEMARK;
	if (v->kind != T_IDENT)
		return D_OTHER;
	switch (v->id) {
	case KW_if: return D_IF;
	case KW_ifdef: return D_IFDEF;
	case KW_ifndef: return D_IFNDEF;
	case KW_elif: return D_ELIF;
	case KW_elifdef: return D_ELIFDEF;
	case KW_elifndef: return D_ELIFNDEF;
	case KW_else: return D_ELSE;
	case KW_endif: return D_ENDIF;
	case KW_define: return D_DEFINE;
	case KW_undef: return D_UNDEF;
	case KW_include: return D_INCLUDE;
	case KW_include_next: return D_INCLUDE_NEXT;
	case KW_pragma: return D_PRAGMA;
	case KW_error: return D_ERROR;
	case KW_warning: return D_WARNING;
	case KW_line: return D_LINE;
	}
	if (spelled(f, v, "import"))
		return D_IMPORT;
	if (spelled(f, v, "ident") || spelled(f, v, "sccs"))
		return D_IDENT;
	return D_OTHER;
}

static pfile *pfile_build(const vlex_file *f)
{
	const vtok *t = f->tok;
	uint32_t n = f->ntok, nd = 0, sp = 0, *stk;
	pfile *fi = salloc(sizeof(*fi));
	pdir *d;

	for (uint32_t i = 0; i < n; i++)
		nd += (t[i].flags & TF_SOL) && t[i].kind == T_HASH;
	d = salloc((nd + 1) * sizeof(*d));
	stk = malloc((nd + 1) * sizeof(*stk));
	nd = 0;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t e, k;

		if (!((t[i].flags & TF_SOL) && t[i].kind == T_HASH))
			continue;
		for (e = i + 1; e < n && !(t[e].flags & TF_SOL); e++)
			;
		k = dir_kind(f, i + 1, e);
		d[nd] = (pdir){ i, e, 0, k };
		switch (k) {
		case D_IF: case D_IFDEF: case D_IFNDEF:
			stk[sp++] = nd;
			break;
		case D_ELIF: case D_ELIFDEF: case D_ELIFNDEF: case D_ELSE:
			if (!sp) { fi->bad = 1; break; }
			d[stk[sp - 1]].next = nd;
			stk[sp - 1] = nd;
			break;
		case D_ENDIF:
			if (!sp) { fi->bad = 1; break; }
			d[stk[--sp]].next = nd;
			break;
		}
		nd++;
		i = e - 1;
	}
	if (sp)
		fi->bad = 1;
	free(stk);
	fi->dir = d;
	fi->mac = salloc((nd + 1) * sizeof(*fi->mac));	/* fresh: zero */
	fi->inc = salloc((nd + 1) * sizeof(*fi->inc));
	fi->ndir = nd;
	/*
	 * Include guard: the file is one #ifndef X / #if !defined(X) group
	 * (no #else/#elif) with nothing before or after it.
	 */
	if (!fi->bad && nd >= 2 && d[0].tok == 0) {
		uint32_t g = 0, e = d[0].end;

		if (d[0].kind == D_IFNDEF && e == 3 && t[2].kind == T_IDENT)
			g = t[2].id;
		else if (d[0].kind == D_IF && t[2].kind == T_EXCLAIM && t[3].kind == T_IDENT &&
			 t[3].id == KW_defined) {
			if (e == 5 && t[4].kind == T_IDENT)
				g = t[4].id;
			else if (e == 7 && t[4].kind == T_LPAREN && t[5].kind == T_IDENT &&
				 t[6].kind == T_RPAREN)
				g = t[5].id;
		}
		if (g) {
			uint32_t k = d[0].next;

			if (d[k].kind == D_ENDIF && d[k].end == n)
				fi->guard = g;
		}
	}
	return fi;
}

static const pfile *pfile_get(const vlex_file *f)
{
	vlex_file *w = (vlex_file *)f;
	void *fi = __atomic_load_n(&w->pp, __ATOMIC_ACQUIRE), *z = NULL;

	if (likely(fi != NULL))
		return fi;
	fi = pfile_build(f);
	if (!__atomic_compare_exchange_n(&w->pp, &z, fi, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return z;	/* lost the race; ours is wasted */
	return fi;
}

/* 1-based line of byte offset off */
static uint32_t file_line(const vlex_file *f, const pfile *cfi, uint32_t off)
{
	pfile *fi = (pfile *)cfi;
	uint32_t *l = __atomic_load_n(&fi->lines, __ATOMIC_ACQUIRE), n, lo, hi;

	if (!l) {
		uint32_t *z = NULL, c = 0;

		for (uint32_t i = 0; i < f->size; i++)
			c += f->src[i] == '\n';
		l = salloc((c + 1) * sizeof(*l));
		c = 0;
		for (uint32_t i = 0; i < f->size; i++)
			if (f->src[i] == '\n')
				l[c++] = i;
		fi->nlines = c;
		if (!__atomic_compare_exchange_n(&fi->lines, &z, l, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			l = z;
	}
	n = __atomic_load_n(&fi->nlines, __ATOMIC_ACQUIRE);
	/* number of newlines before off */
	lo = 0, hi = n;
	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (l[mid] < off)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo + 1;
}

/* ---------------- arenas ---------------- */

typedef struct arena {
	char *base, *p, *end;
} arena;

static void arena_init(arena *a, size_t n)
{
	a->base = a->p = vm(n);
	a->end = a->base + n;
}

static inline void *alloc(arena *a, size_t n)
{
	char *r = a->p;

	n = (n + 7) & ~(size_t)7;
	if (unlikely((size_t)(a->end - r) < n)) {
		fprintf(stderr, "vcc: preprocessor arena full\n");
		abort();
	}
	a->p = r + n;
	return r;
}

/* ---------------- macros ---------------- */

enum { MK_OBJ, MK_FUNC, MK_BUILTIN };
enum {
	MF_VARIADIC = 1, MF_PASTE = 2, MF_VAOPT = 4,
	MF_BADHASH = 8,		/* '#' not before a parameter: an error unless assembler */
};
enum {
	B_NONE, B_FILE, B_LINE, B_COUNTER, B_DATE, B_TIME, B_TIMESTAMP,
	B_BASE_FILE, B_FILE_NAME, B_INCLUDE_LEVEL, B_PRAGMA,
	B_HAS_ATTRIBUTE, B_HAS_BUILTIN, B_HAS_FEATURE, B_HAS_EXTENSION,
	B_HAS_C_ATTRIBUTE, B_HAS_CPP_ATTRIBUTE, B_HAS_DECLSPEC, B_HAS_WARNING,
	B_HAS_CONSTEXPR_BUILTIN, B_IS_TARGET_ARCH, B_IS_TARGET_OS, B_IS_TARGET_VENDOR,
	B_IS_TARGET_ENVIRONMENT, B_IS_TARGET_VARIANT_OS, B_IS_TARGET_VARIANT_ENVIRONMENT,
	B_BUILDING_MODULE,
	B_IS_IDENTIFIER, B_HAS_INCLUDE, B_HAS_INCLUDE_NEXT,
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
	const char *src;
	const uint8_t *pidx;	/* per body token: parameter index + 1, or 0 */
	uint32_t nbody, name;
	uint32_t hash;		/* canon_macro */
	uint16_t nparam;
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
	uint32_t i, n;
	uint32_t loc;		/* C_FILE: file base; C_VTOK: expansion loc */
	uint32_t mid;		/* macro to re-enable when popped */
	const vtok *vt;
	const ptok *pt;
	const char *src;
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
} ctx;

typedef struct fent {
	uint32_t base;
	const vlex_file *f;
	const pfile *fi;
	const char *name;	/* for __FILE__ (prefix map applied) */
	uint32_t namelen;
	const char *spath;	/* as spelled, for dependency files */
	uint8_t sys;		/* a system header (found in a system directory, or included by one) */
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

typedef struct hseg {
	uint32_t at;		/* own-text offset (tokens: own-token index) the child goes before */
	uint32_t cdelta;	/* child's counter at entry - ours */
	const hent *child;
	uint32_t lbase;		/* tokens: child's location base - ours */
} hseg;

struct hent {
	hent *next;
	const hread *rg;	/* inputs: include guards (v: HDEF or 0); checked first */
	const uint32_t *ru;	/* inputs: ids read while undefined */
	const hread *rv;	/* inputs: other reads */
	const hread *dl;	/* delta: (id, new value) */
	const honce *ord;
	const vlex_file *const *oadd;
	const hfent *fe;
	const char *text;	/* own text */
	const ptok *tk;		/* token entries: own tokens (loc: offset from the base + 1, 0: none) */
	const hctr *ctr;
	const hcread *crd;
	const hseg *seg;
	size_t ntext, total;	/* own text (tokens); with the children */
	int32_t level;		/* __INCLUDE_LEVEL__ used: the include depth it ran at (-1: not used) */
	uint32_t ndirs, psig;	/* leading search directories its lookups depended on; their id */
	uint32_t nrg, nru, nrv, ndl, nord, noadd, nfe, nctr, ncrd, nseg, span, counter_delta;
	uint8_t has_text, first_sol, col_end, pend_out;
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
enum { HT_R = 1, HT_W = 2, HT_G = 4 /* the read was an include guard check */ };

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
} hlev;

struct pp {
	uintptr_t *mdir[NMDIR];
	const pp_opts *o;
	ctx *cx;
	int depth, nfile;
	arena perm, tmp;
	ptok *sv;		/* scratch token stack */
	size_t svn;
	fent *fe;
	uint32_t nfe, next_base;
	uint64_t ntok_out;	/* tokens pp_next delivered */
	uint64_t skipped_tok;	/* tokens of regions pp_skip_top dropped */
	pp_topev *tev;		/* top-level region events (pp_top_events) */
	uint32_t ntev;
	uint8_t tev_full, top_rec;
	const void *hc_last;	/* the entry hc_apply applied last */
	uint8_t pend;		/* SOL/SPACE owed to the next token (empty expansions) */
	int preexp;		/* in argument pre-expansion */
	int errors;
	unsigned counter;
	const vlex_file **once;
	uint32_t nonce;
	struct { uint32_t id; uintptr_t m; } *pushed;
	uint32_t npushed;
	vlex_file builtin;
	const char *base_name;
	uint32_t base_namelen;
	jmp_buf *fatal_jb;	/* fatal errors end the TU here */
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
	/* -E text (pp_text) */
	char *out;
	size_t nout, capout;
	unsigned col;
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
		cprov *cp;	/* indexed by ptok.ctr */
		uint32_t ncp;
		ptok *tk;	/* tokens handed out (token mode) */
		uint32_t ntk;
		char *sb;	/* their spellings that live in per-TU memory */
		size_t nsb;
	} rec;
	uint64_t *rst[NMDIR];	/* per id: serial << 32 | index in rec.t */
};

/* ---------------- diagnostics ---------------- */

static const fent *loc_fent(const pp *p, uint32_t loc)
{
	uint32_t lo = 0, hi = p->nfe;

	while (hi - lo > 1) {
		uint32_t mid = (lo + hi) / 2;

		if (p->fe[mid].base <= loc)
			lo = mid;
		else
			hi = mid;
	}
	return p->nfe ? &p->fe[lo] : NULL;
}

static uint32_t loc_line(const pp *p, uint32_t loc)
{
	const fent *e = loc_fent(p, loc);
	uint32_t l, fe;

	if (!e)
		return 0;
	l = file_line(e->f, e->fi, loc - e->base);
	fe = (uint32_t)(e - p->fe);
	for (uint32_t i = p->nlm; i-- > 0;)
		if (p->lm[i].fe == fe && p->lm[i].off <= loc - e->base)
			return p->lm[i].line + (l - p->lm[i].pline);
	return l;
}

/* the presumed file name at loc in file context fc (#line can change it) */
static const char *presumed_name(const pp *p, uint32_t fe, uint32_t loc, uint32_t *len)
{
	const fent *e = &p->fe[fe];

	for (uint32_t i = p->nlm; i-- > 0;)
		if (p->lm[i].fe == fe && p->lm[i].off <= loc - e->base && p->lm[i].name) {
			*len = p->lm[i].namelen;
			return p->lm[i].name;
		}
	*len = e->namelen;
	return e->name;
}

static void diag(pp *p, uint32_t loc, const char *what, const char *fmt, ...)
{
	const fent *e = loc_fent(p, loc);
	int fd = p->o->errfd ? p->o->errfd : 2;
	char line[2048];
	int n;
	va_list ap;

	if (e)
		n = snprintf(line, sizeof(line), "%s:%u: %s: ", e->name, loc_line(p, loc), what);
	else
		n = snprintf(line, sizeof(line), "vcc: %s: ", what);
	va_start(ap, fmt);
	n += vsnprintf(line + n, sizeof(line) - (size_t)n - 1, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof(line) - 2)
		n = (int)sizeof(line) - 2;
	line[n++] = '\n';
	(void)!write(fd, line, (size_t)n);
	if (what[0] == 'e' || what[0] == 'f')
		p->errors++;
	else
		p->warns++;
	if (what[0] == 'f') {
		if (p->fatal_jb)
			longjmp(*p->fatal_jb, 1);
		exit(1);
	}
}

/* ---------------- exported for the parser ---------------- */

int pp_in_system_header(const pp *p, uint32_t loc)
{
	const fent *e = loc_fent(p, loc);

	return e && loc - e->base <= e->f->size && e->sys;
}

int pp_where(const pp *p, uint32_t loc, const char **name, uint32_t *namelen, uint32_t *line, uint32_t *col)
{
	const fent *e = loc_fent(p, loc);
	uint32_t off, b;

	if (!e || loc - e->base > e->f->size) {
		*name = "<built-in>", *namelen = 10, *line = *col = 0;
		return 0;
	}
	off = loc - e->base;
	for (b = off; b > 0 && e->f->src[b - 1] != '\n'; b--)
		;
	*line = loc_line(p, loc);
	*col = off - b + 1;
	*name = presumed_name(p, (uint32_t)(e - p->fe), loc, namelen);
	return 1;
}

void pp_vdiag(pp *p, uint32_t loc, const char *what, const char *fmt, va_list ap)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	const char *name;
	uint32_t nl, line, col;
	char buf[2048];
	int n;

	if (pp_where(p, loc, &name, &nl, &line, &col))
		n = snprintf(buf, sizeof(buf), "%.*s:%u:%u: %s: ", (int)nl, name, line, col, what);
	else
		n = snprintf(buf, sizeof(buf), "vcc: %s: ", what);
	n += vsnprintf(buf + n, sizeof(buf) - (size_t)n - 1, fmt, ap);
	if (n > (int)sizeof(buf) - 2)
		n = (int)sizeof(buf) - 2;
	buf[n++] = '\n';
	(void)!write(fd, buf, (size_t)n);
	if (what[0] == 'e' || what[0] == 'f')
		p->errors++;
	else
		p->warns++;
	if (what[0] == 'f') {
		if (p->fatal_jb)
			longjmp(*p->fatal_jb, 1);
		exit(1);
	}
}

#define error(p, loc, ...) diag(p, loc, "error", __VA_ARGS__)
#define fatal(p, loc, ...) diag(p, loc, "fatal error", __VA_ARGS__)
#define warn(p, loc, ...) diag(p, loc, "warning", __VA_ARGS__)

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
		*pg = alloc(&p->perm, MPAGE * sizeof(uintptr_t));
		memset(*pg, 0, MPAGE * sizeof(uintptr_t));
	}
	return &(*pg)[id & (MPAGE - 1)];
}

static inline void mset(pp *p, uint32_t id, const pmacro *m)
{
	*mslot(p, id) = (uintptr_t)m;
}

static inline void mdisable(pp *p, uint32_t id) { *mslot(p, id) |= 1; }
static inline void menable(pp *p, uint32_t id) { *mslot(p, id) &= ~(uintptr_t)1; }

/* ---------------- header cache: recording reads and writes ---------------- */

/* the open regions are not cacheable (something in them depends on more than we record) */
#define HC_BAD(p) do { if ((p)->rec.n) { (p)->rec.nbad = (p)->rec.n; (p)->rec.badline = __LINE__; } } while (0)

static uint64_t *rslot(pp *p, uint32_t id)
{
	uint64_t **pg = &p->rst[id >> MPAGE_BITS];

	if (unlikely(!*pg)) {
		*pg = alloc(&p->perm, MPAGE * sizeof(uint64_t));
		memset(*pg, 0, MPAGE * sizeof(uint64_t));
	}
	return &(*pg)[id & (MPAGE - 1)];
}

#define HC_MAXT (1u << 24)

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
		t->flags |= HT_W;
}

/* a read of id (entry e) while recording; def: only definedness matters */
static __attribute__((noinline)) void rec_read(pp *p, uint32_t id, uintptr_t e, int def)
{
	e &= ~(uintptr_t)1;
	touch_read(p, id, def ? (e ? HDEF : 0) : e, def == 2 ? HT_G : 0);
}

#define HC_MAXCP 65535u

static uint16_t new_cprov(pp *p, uint32_t val, uint32_t off, uint32_t len, uint16_t next)
{
	if (p->rec.ncp >= HC_MAXCP || len > 0xffff) {
		HC_BAD(p);
		return 0;
	}
	p->rec.cp[++p->rec.ncp] = (cprov){ val, off, (uint16_t)len, next };
	return (uint16_t)p->rec.ncp;
}

/* chain a shifted by d, followed by chain tail (shared) */
static uint16_t cp_chain(pp *p, uint16_t a, uint32_t d, uint16_t tail)
{
	if (!a)
		return tail;
	{
		const cprov c = p->rec.cp[a];
		uint16_t n = cp_chain(p, c.next, d, tail);

		return new_cprov(p, c.val, c.off + d, c.len, n);
	}
}

#define HC_MAXCRD 65536u

/* a read of an identifier with __COUNTER__ digits: at replay its name differs */
static __attribute__((noinline)) void rec_ctr_read(pp *p, const ptok *t, uintptr_t e)
{
	const cprov *c = &p->rec.cp[t->ctr];
	char *d;

	if (e || c->next || p->rec.ncrd >= HC_MAXCRD || t->len > 4096) {
		HC_BAD(p);	/* such a name defined as a macro, or two runs: give up */
		return;
	}
	d = alloc(&p->perm, t->len + 1);
	memcpy(d, t->s, t->len);	/* pasted: the spelling is clean */
	p->rec.crd[p->rec.ncrd++] = (hcrd){ d, t->len, c->off, c->len, c->val };
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

/* #define / #undef / pop_macro */
static void mdef(pp *p, uint32_t id, const pmacro *m)
{
	if (unlikely(p->rec.on))
		touch_write(p, id);
	mset(p, id, m);
}

/* ---------------- header cache: shared table ---------------- */

static struct {
	_Atomic unsigned long hit, miss, rec, uncacheable, full, bytes_replayed, bytes_rec, mem;
	_Atomic unsigned long nru, nrv, ndl, nfe, nvalid, nchecked;
} hstat;

#define HKEY_BITS 16
static _Atomic(hkey *) hkey_tab[1u << HKEY_BITS];
static size_t hc_mem_max;

/*
 * Signatures.  The base signature is every option that decides what a
 * header does except the macros and the search path; the search path is an
 * input of a region only as far as its lookups went: an entry records how
 * many leading search directories its #include / __has_include lookups
 * depended on (up to the one a file was found in; all of them for a
 * lookup that failed), and matches TUs whose search path starts with the
 * same directories.  (kbuild adds -I for each source directory at the end:
 * that does not split the cache.)
 */
static pthread_mutex_t sig_mu = PTHREAD_MUTEX_INITIALIZER;
static char *sig_str[1 << 16];
static size_t sig_len[1 << 16];
static uint32_t nsig;

static uint32_t sig_intern(char *b, size_t n)
{
	uint32_t r = HNONE;

	pthread_mutex_lock(&sig_mu);
	for (uint32_t i = 0; i < nsig; i++)
		if (sig_len[i] == n && !memcmp(sig_str[i], b, n)) {
			r = i;
			break;
		}
	if (r == HNONE && nsig < (1 << 16)) {
		char *c = malloc(n ? n : 1);

		memcpy(c, b, n);
		sig_str[nsig] = c;
		sig_len[nsig] = n;
		r = nsig++;
	}
	pthread_mutex_unlock(&sig_mu);
	return r;
}

/* base signature into *sig, prefix ids into psig[0..ndirs]; false if too long */
static bool sig_of(const pp_opts *o, uint32_t *sig, uint32_t *psig)
{
	char *b = malloc(1 << 17);
	size_t n = 0;
	bool ok = true;

#define SIGS(x) do { const char *s_ = (x) ? (x) : ""; size_t l_ = strlen(s_); \
	if (n + l_ + 8 < (1 << 17)) { memcpy(b + n, s_, l_); n += l_; b[n++] = 0; } else ok = false; } while (0)
	n += (size_t)snprintf(b, 64, "%d %d %d %d", o->nfwdirs, o->unsigned_char, o->asm_mode, o->target) + 1;
	for (int i = 0; i < o->nfwdirs; i++)
		SIGS(o->fwdirs[i]);
	SIGS(o->cwd);
	SIGS(o->prefix_from);
	SIGS(o->prefix_to);
	*sig = sig_intern(b, n);
	n = 0;
	psig[0] = sig_intern(b, 0);
	for (int i = 0; i < o->ndirs && ok; i++) {
		/* each directory with whether it is a quote-only and a system directory */
		b[n++] = (char)('0' + (i < o->nquote) + 2 * (o->nsys && i >= o->nsys));
		SIGS(o->dirs[i]);
		psig[i + 1] = sig_intern(b, n);
	}
#undef SIGS
	free(b);
	return ok && *sig != HNONE;
}

static hkey *hkey_get(uint32_t sig, const vlex_file *f, int32_t sdir, uint32_t nfile, const char *spath, uint8_t sys,
		     bool make)
{
	size_t sl = strlen(spath);
	uint64_t h = ((uint64_t)(uintptr_t)f * 0x9E3779B97F4A7C15ull) ^
		     ((uint64_t)sig << 40 | (uint64_t)(uint32_t)sdir << 16 | (uint64_t)sys << 15 | nfile);
	hkey *mine = NULL;

	h = (h ^ vintern_hash(spath, sl)) * 0xff51afd7ed558ccdull;
	h ^= h >> 29;
	for (uint32_t i = (uint32_t)(h >> (64 - HKEY_BITS)), k = 0; k < (1u << HKEY_BITS);
	     i = (i + 1) & ((1u << HKEY_BITS) - 1), k++) {
		hkey *e = atomic_load_explicit(&hkey_tab[i], memory_order_acquire);

		if (!e) {
			if (!make)
				return NULL;
			if (!mine) {
				char *sp = salloc(sl + 1);

				memcpy(sp, spath, sl + 1);
				mine = salloc(sizeof(*mine));
				mine->hash = h;
				mine->f = f;
				mine->spath = sp;
				mine->sig = sig;
				mine->nfile = nfile;
				mine->sdir = sdir;
				mine->sys = sys;
			}
			if (atomic_compare_exchange_strong_explicit(&hkey_tab[i], &e, mine, memory_order_acq_rel,
								    memory_order_acquire))
				return mine;
		}
		if (e->hash == h && e->f == f && e->sig == sig && e->sdir == sdir && e->nfile == nfile && e->sys == sys &&
		    !strcmp(e->spath, spath))
			return e;
	}
	return NULL;
}

/* ---------------- -E text ---------------- */

/* the -E buffer of this thread's last pp, kept for the next one (no unmap, no page faults) */
static _Thread_local char *out_spare;
static _Thread_local size_t out_spare_cap;

static void out_reserve(pp *p, size_t n)
{
	size_t nc;
	char *np;

	if (likely(p->nout + n <= p->capout))
		return;
	if (!p->out && out_spare && out_spare_cap >= n) {
		p->out = out_spare, p->capout = out_spare_cap;
		out_spare = NULL, out_spare_cap = 0;
		return;
	}
	nc = p->capout ? p->capout * 2 : 1u << 24;
	while (nc < p->nout + n)
		nc *= 2;
	np = vm(nc);
	if (p->out) {
		memcpy(np, p->out, p->nout);
		munmap(p->out, p->capout);
	}
	p->out = np;
	p->capout = nc;
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

/* ---------------- tokens ---------------- */

static inline void vfill(ptok *t, const vtok *v, const char *src, uint32_t loc)
{
	t->s = src + v->off;
	t->len = v->len;
	t->id = v->id;
	t->loc = loc;
	t->kind = v->kind;
	t->flags = v->flags & (TF_SOL | TF_SPACE | TF_DIRTY | TF_RAW);
	t->ctr = 0;
}

static inline void mkeof(ptok *t)
{
	memset(t, 0, sizeof(*t));
	t->kind = T_EOF;
}

static size_t splice_at(const char *s, size_t i, size_t n)
{
	size_t j;

	if (s[i] != '\\')
		return 0;
	for (j = i + 1; j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\f' || s[j] == '\v'); j++)
		;
	if (j < n && s[j] == '\n')
		return j + 1 - i;
	if (j < n && s[j] == '\r')
		return j + 1 - i + (j + 1 < n && s[j + 1] == '\n');
	return 0;
}

const char *pp_spell(const ptok *t, char *buf, size_t *len)
{
	size_t n = 0, l;

	if (!(t->flags & TF_DIRTY)) {
		*len = t->len;
		return t->s;
	}
	for (size_t i = 0; i < t->len;) {
		if ((l = splice_at(t->s, i, t->len))) {
			i += l;
			continue;
		}
		buf[n++] = t->s[i++];
	}
	*len = n;
	return buf;
}

/* clean spelling appended to dst; returns the new end */
static char *spell_to(char *dst, const ptok *t)
{
	size_t n;
	const char *s = pp_spell(t, dst, &n);

	if (s != dst)
		memcpy(dst, s, n);
	return dst + n;
}

static inline void svpush(pp *p, const ptok *t)
{
	if (unlikely(p->svn == SVMAX)) {
		fprintf(stderr, "vcc: expansion too large\n");
		abort();
	}
	p->sv[p->svn++] = *t;
}

/* sv[base..svn) to tmp; svn = base */
static ptok *svtake(pp *p, size_t base, uint32_t *n)
{
	size_t k = p->svn - base;
	ptok *r = alloc(&p->tmp, (k ? k : 1) * sizeof(ptok));

	memcpy(r, p->sv + base, k * sizeof(ptok));
	p->svn = base;
	*n = (uint32_t)k;
	return r;
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

static void push_ptok(pp *p, const ptok *t, uint32_t n, uint32_t mid, uint8_t first, int barrier)
{
	ctx *c = push(p);

	c->type = C_PTOK;
	c->pt = t;
	c->n = n;
	c->mid = mid;
	c->first = first;
	c->barrier = (uint8_t)barrier;
}

/* tokens [b, e) of the current directive line as a barrier context */
static void push_line(pp *p, const ctx *fc, uint32_t b, uint32_t e)
{
	ctx *c = push(p);

	c->type = C_VTOK;
	c->vt = fc->vt + b;
	c->n = e > b ? e - b : 0;
	c->src = fc->src;
	c->loc = fc->loc;
	c->offloc = 1;
	c->barrier = 1;
}

/* pop everything down to and including the barrier context at depth d */
static void pop_to(pp *p, int d)
{
	while (p->depth > d) {
		ctx *c = &p->cx[p->depth - 1];

		if (c->type == C_FILE) {
			p->nfile--;
		} else if (c->mid) {
			menable(p, c->mid);
		}
		p->depth--;
	}
}

static int directive(pp *p, ctx *c, ptok *out);
static void hc_end(pp *p);
static bool replay_seg(pp *p, ctx *c);
static void replay_tok(pp *p, ctx *c, ptok *t);

/* a top-level region event (pp_top_events) at the current delivered position */
static void top_event(pp *p, const void *e, uint32_t base, unsigned ctr, uint8_t end, uint8_t hit)
{
	if (p->tev_full)
		return;
	if (!p->tev)
		p->tev = alloc(&p->perm, 1024 * sizeof(pp_topev));
	if (p->ntev == 1024) {
		p->tev_full = 1;
		return;
	}
	p->tev[p->ntev++] = (pp_topev){ e, p->ntok_out, base, ctr, end, hit };
}

/* the next token, no macro expansion; runs directives in file contexts */
static void raw(pp *p, ptok *t)
{
	for (;;) {
		ctx *c = &p->cx[p->depth - 1];

		if (likely(c->i < c->n)) {
			if (c->type == C_PTOK) {
				*t = c->pt[c->i++];
			} else if (c->type == C_VTOK) {
				const vtok *v = &c->vt[c->i++];

				vfill(t, v, c->src, c->offloc ? c->loc + v->off : c->loc);
			} else if (c->type == C_REPLAY) {
				if (replay_seg(p, c))
					continue;
				replay_tok(p, c, t);
			} else {
				const vtok *v = &c->vt[c->i];

				if (unlikely((v->flags & TF_SOL) && v->kind == T_HASH)) {
					if (directive(p, c, t))
						return;
					continue;
				}
				c->i++;
				vfill(t, v, c->src, c->loc + v->off);
			}
			if (c->first) {
				t->flags = (t->flags & ~(TF_SOL | TF_SPACE)) | (c->first & 3);
				c->first = 0;
			}
			if (p->pend) {
				t->flags |= p->pend;
				p->pend = 0;
			}
			return;
		}
		if (c->type == C_REPLAY) {
			if (replay_seg(p, c))
				continue;
			if (c->top)
				p->pend |= c->he->pend_out;
			if (c->top == 2)
				top_event(p, c->he, 0, 0, 1, 1);
			p->depth--;
			continue;
		}
		if (c->barrier) {
			mkeof(t);
			return;
		}
		if (c->type == C_FILE) {
			if (p->nfile == 1) {
				mkeof(t);
				return;
			}
			if (unlikely(p->rec.on) && p->depth == p->rec.lev[p->rec.n - 1].depth)
				hc_end(p);
			p->nfile--;
			p->depth--;
			continue;
		}
		if (c->mid)
			menable(p, c->mid);
		if (c->first)
			p->pend |= c->first & 3;
		p->depth--;
	}
}

/* is the next token '(' ?  (clang's isNextPPTokenLParen: no popping) */
static bool peek_lparen(const pp *p)
{
	for (int d = p->depth - 1; d >= 0; d--) {
		const ctx *c = &p->cx[d];

		if (c->i < c->n)
			return (c->type == C_PTOK || c->type == C_REPLAY ? c->pt[c->i].kind : c->vt[c->i].kind) == T_LPAREN;
		if (c->type == C_FILE || c->barrier)
			return false;
	}
	return false;
}

static int expand(pp *p, const pmacro *m, ptok *t);

/* the next fully expanded token */
static void next_exp(pp *p, ptok *t)
{
	for (;;) {
		uintptr_t e;
		const pmacro *m;

		raw(p, t);
		if (t->kind != T_IDENT || (t->flags & PF_NOEXP))
			return;
		e = mread_tok(p, t, 0);
		if (likely(!e))
			return;
		if (e & 1) {
			t->flags |= PF_NOEXP;
			return;
		}
		m = (const pmacro *)e;
		if (m->kind == MK_FUNC && !peek_lparen(p))
			return;
		if (expand(p, m, t))
			return;
	}
}

/* ---------------- arguments ---------------- */

typedef struct margs {
	const ptok *tok;
	uint32_t *st, *ln;	/* per parameter: start, length in tok */
	const ptok **pre;	/* pre-expanded, lazily */
	uint32_t *preln;
	uint8_t *prest;		/* 0: not computed, 1: done */
	const ptok **str;	/* stringified */
} margs;

static bool collect_(pp *p, const pmacro *m, const ptok *name, margs *a, ptok *rp)
{
	uint32_t np = m->nparam, slots = np ? np : 1, k = 0, depth = 0, n;
	size_t base = p->svn;
	bool va = m->flags & MF_VARIADIC;
	ptok t;

	a->st = alloc(&p->tmp, slots * sizeof(uint32_t));
	a->ln = alloc(&p->tmp, slots * sizeof(uint32_t));
	raw(p, &t);		/* '(' */
	a->st[0] = 0;
	for (;;) {
		raw(p, &t);
		if (unlikely(t.kind == T_EOF)) {
			error(p, name->loc, "unterminated function-like macro invocation");
			p->svn = base;
			return false;
		}
		if (t.kind == T_LPAREN) {
			depth++;
		} else if (t.kind == T_RPAREN) {
			if (!depth)
				break;
			depth--;
		} else if (t.kind == T_COMMA && !depth && !(va && k + 1 >= np)) {
			if (k + 1 >= slots) {
				k++;
				continue;	/* too many: counted, reported below */
			}
			a->ln[k] = (uint32_t)(p->svn - base) - a->st[k];
			a->st[++k] = (uint32_t)(p->svn - base);
			continue;
		} else if (unlikely(t.kind == P_DIRECTIVE)) {
			warn(p, t.loc, "directive in macro arguments ignored");
			continue;
		}
		svpush(p, &t);
	}
	*rp = t;
	if (k >= slots) {
		error(p, name->loc, "too many arguments provided to function-like macro invocation");
		p->svn = base;
		return false;
	}
	a->ln[k] = (uint32_t)(p->svn - base) - a->st[k];
	if (k + 1 < np) {
		if (va && k + 2 == np) {
			/* GNU: the variadic argument left out entirely */
			a->st[k + 1] = (uint32_t)(p->svn - base);
			a->ln[k + 1] = 0;
		} else {
			error(p, name->loc, "too few arguments provided to function-like macro invocation");
			p->svn = base;
			return false;
		}
	} else if (np == 0 && a->ln[0]) {
		error(p, name->loc, "too many arguments provided to function-like macro invocation");
		p->svn = base;
		return false;
	}
	a->tok = svtake(p, base, &n);
	a->pre = alloc(&p->tmp, slots * sizeof(*a->pre));
	a->preln = alloc(&p->tmp, slots * sizeof(uint32_t));
	a->prest = alloc(&p->tmp, slots);
	a->str = alloc(&p->tmp, slots * sizeof(*a->str));
	memset(a->prest, 0, slots);
	memset(a->str, 0, slots * sizeof(*a->str));
	return true;
}

static bool collect(pp *p, const pmacro *m, const ptok *name, margs *a, ptok *rp)
{
	bool r;

	p->collecting++;
	r = collect_(p, m, name, a, rp);
	p->collecting--;
	return r;
}

/* clang's MacroArgs::ArgNeedsPreexpansion: any identifier with a macro */
static bool needs_pre(pp *p, const ptok *t, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (t[i].kind == T_IDENT && mread_tok(p, &t[i], 1))
			return true;
	return false;
}

static void preexpand(pp *p, margs *a, uint32_t k)
{
	const ptok *at = a->tok + a->st[k];
	uint32_t n = a->ln[k];
	uint8_t pend = p->pend;
	int d = p->depth;
	size_t base = p->svn;
	ptok t;

	a->prest[k] = 1;
	if (!needs_pre(p, at, n)) {
		a->pre[k] = at;
		a->preln[k] = n;
		return;
	}
	push_ptok(p, at, n, 0, 0, 1);
	p->pend = 0;
	p->preexp++;
	for (;;) {
		next_exp(p, &t);
		if (t.kind == T_EOF)
			break;
		svpush(p, &t);
	}
	p->preexp--;
	pop_to(p, d);
	p->pend = pend;
	a->pre[k] = svtake(p, base, &a->preln[k]);
}

static bool is_strlit(int k)
{
	return k >= T_CHAR && k <= T_U32STRING;
}

/* "#x" (clang's MacroArgs::StringifyArgument) */
static void stringify(pp *p, const ptok *t, uint32_t n, ptok *out)
{
	size_t cap = 3;
	char *b, *w;

	for (uint32_t i = 0; i < n; i++)
		cap += 2 * t[i].len + 1;
	/* __COUNTER__ digit runs, in order: (run, its offset in the result) */
	uint16_t runs[64];
	uint32_t roff[64], nr = 0;

	b = w = alloc(&p->tmp, cap + 16);
	*w++ = '"';
	for (uint32_t i = 0; i < n; i++) {
		uint16_t rc = unlikely(t[i].ctr) && p->rec.on ? t[i].ctr : 0;

		if (i && (t[i].flags & (TF_SPACE | TF_SOL)))
			*w++ = ' ';
		if (is_strlit(t[i].kind)) {
			char tmp[t[i].len + 1];
			size_t l;
			const char *s = pp_spell(&t[i], tmp, &l);

			for (size_t j = 0; j < l; j++) {
				/* digits are never escaped: a run starts where its first digit lands */
				while (rc && p->rec.cp[rc].off == j) {
					if (nr < 64) {
						runs[nr] = rc;
						roff[nr++] = (uint32_t)(w - b);
					} else {
						HC_BAD(p);
					}
					rc = p->rec.cp[rc].next;
				}
				if (s[j] == '\\' || s[j] == '"')
					*w++ = '\\';
				if (s[j] == '\n') {
					*w++ = '\\';
					*w++ = 'n';
					continue;
				}
				*w++ = s[j];
			}
			if (rc)
				HC_BAD(p);
		} else {
			for (; rc; rc = p->rec.cp[rc].next) {
				if (nr < 64) {
					runs[nr] = rc;
					roff[nr++] = (uint32_t)(w - b) + p->rec.cp[rc].off;
				} else {
					HC_BAD(p);
				}
			}
			w = spell_to(w, &t[i]);
		}
	}
	/* a trailing unescaped backslash would escape the quote: drop it */
	if (w - b >= 2 && w[-1] == '\\') {
		size_t bs = 0;

		for (char *q = w - 1; q > b && *q == '\\'; q--)
			bs++;
		if (bs & 1)
			w--;
	}
	*w++ = '"';
	memset(w, 0, 16);
	memset(out, 0, sizeof(*out));
	out->s = b;
	out->len = (uint32_t)(w - b);
	out->kind = T_STRING;
	out->loc = n ? t[0].loc : 0;
	for (uint32_t k = nr; k-- > 0;) {
		const cprov c = p->rec.cp[runs[k]];

		out->ctr = new_cprov(p, c.val, roff[k], c.len, out->ctr);
	}
}

/* ---------------- token pasting ---------------- */

static const struct { const char *s; uint8_t k; } puncts[] = {
	{ "[", T_LSQUARE }, { "]", T_RSQUARE }, { "(", T_LPAREN }, { ")", T_RPAREN },
	{ "{", T_LBRACE }, { "}", T_RBRACE }, { ".", T_PERIOD }, { "...", T_ELLIPSIS },
	{ "&", T_AMP }, { "&&", T_AMPAMP }, { "&=", T_AMPEQ }, { "*", T_STAR },
	{ "*=", T_STAREQ }, { "+", T_PLUS }, { "++", T_PLUSPLUS }, { "+=", T_PLUSEQ },
	{ "-", T_MINUS }, { "->", T_ARROW }, { "--", T_MINUSMINUS }, { "-=", T_MINUSEQ },
	{ "~", T_TILDE }, { "!", T_EXCLAIM }, { "!=", T_EXCLAIMEQ }, { "/", T_SLASH },
	{ "/=", T_SLASHEQ }, { "%", T_PERCENT }, { "%=", T_PERCENTEQ }, { "<", T_LESS },
	{ "<<", T_LESSLESS }, { "<=", T_LESSEQ }, { "<<=", T_LESSLESSEQ }, { ">", T_GREATER },
	{ ">>", T_GREATERGREATER }, { ">=", T_GREATEREQ }, { ">>=", T_GREATERGREATEREQ },
	{ "^", T_CARET }, { "^=", T_CARETEQ }, { "|", T_PIPE }, { "||", T_PIPEPIPE },
	{ "|=", T_PIPEEQ }, { "?", T_QUESTION }, { ":", T_COLON }, { ";", T_SEMI },
	{ "=", T_EQUAL }, { "==", T_EQUALEQUAL }, { ",", T_COMMA }, { "#", T_HASH },
	{ "##", T_HASHHASH }, { "<:", T_LSQUARE }, { ":>", T_RSQUARE }, { "<%", T_LBRACE },
	{ "%>", T_RBRACE }, { "%:", T_HASH }, { "%:%:", T_HASHHASH },
};

static bool idch(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
	       c == '_' || c == '$' || c >= 0x80;
}

/* the kind of the single token spelled s[0..n), or -1 */
static int classify(const char *s, size_t n, int rkind)
{
	size_t i;

	if (!n)
		return -1;
	if (idch((unsigned char)s[0]) && !(s[0] >= '0' && s[0] <= '9')) {
		for (i = 0; i < n && idch((unsigned char)s[i]); i++)
			;
		if (i == n)
			return T_IDENT;
		/* encoding prefix + literal */
		if (is_strlit(rkind)) {
			size_t pl = i;

			if (s[pl] == '"' || s[pl] == '\'') {
				bool ch = s[pl] == '\'';

				if (pl == 1 && s[0] == 'L') return ch ? T_WCHAR : T_WSTRING;
				if (pl == 1 && s[0] == 'u') return ch ? T_U16CHAR : T_U16STRING;
				if (pl == 1 && s[0] == 'U') return ch ? T_U32CHAR : T_U32STRING;
				if (pl == 2 && s[0] == 'u' && s[1] == '8') return ch ? T_U8CHAR : T_U8STRING;
			}
		}
		return -1;
	}
	if ((s[0] >= '0' && s[0] <= '9') || (s[0] == '.' && n > 1 && s[1] >= '0' && s[1] <= '9')) {
		for (i = 1; i < n; i++) {
			unsigned char c = (unsigned char)s[i];

			if ((c == '+' || c == '-') && strchr("eEpP", s[i - 1]))
				continue;
			if (!idch(c) && c != '.')
				return -1;
		}
		return T_NUMBER;
	}
	for (i = 0; i < sizeof(puncts) / sizeof(puncts[0]); i++)
		if (strlen(puncts[i].s) == n && !memcmp(puncts[i].s, s, n))
			return puncts[i].k;
	return -1;
}

static bool paste_ok(const ptok *l, const ptok *r)
{
	char b[l->len + r->len + 1], *w = spell_to(b, l);

	w = spell_to(w, r);
	return classify(b, (size_t)(w - b), r->kind) >= 0;
}

static ptok paste(pp *p, const ptok *l, const ptok *r)
{
	char *b = alloc(&p->tmp, l->len + r->len + 24), *w;
	ptok t = *l;
	int k;
	uint32_t ll;

	w = spell_to(b, l);
	ll = (uint32_t)(w - b);
	w = spell_to(w, r);
	memset(w, 0, 17);
	k = classify(b, (size_t)(w - b), r->kind);
	if (k < 0) {
		error(p, l->loc, "pasting formed '%.*s', an invalid preprocessing token", (int)(w - b), b);
		return *l;
	}
	t.s = b;
	t.len = (uint32_t)(w - b);
	t.kind = (uint8_t)k;
	t.flags = l->flags & (TF_SOL | TF_SPACE);
	t.id = k == T_IDENT ? vintern_copy(b, t.len) : 0;
	if (unlikely(r->ctr) && p->rec.on)
		t.ctr = cp_chain(p, l->ctr, 0, cp_chain(p, r->ctr, ll, 0));
	return t;
}

/* ---------------- expansion ---------------- */

static inline bool hashhash(const ptok *t)
{
	return t->kind == T_HASHHASH && !(t->flags & PF_NOPASTE);
}

/* clang's maybeRemoveCommaBeforeVaArgs (gnu mode) */
static void remove_va_comma(pp *p, size_t base, bool *ngs)
{
	if (p->svn == base || p->sv[p->svn - 1].kind != T_COMMA)
		return;
	p->svn--;
	if (p->svn > base && hashhash(&p->sv[p->svn - 1]))
		p->svn--;
	*ngs = false;
}

/* append n tokens, turning ## into non-pasting ones */
static size_t append(pp *p, const ptok *t, uint32_t n)
{
	size_t first = p->svn;

	for (uint32_t i = 0; i < n; i++) {
		svpush(p, &t[i]);
		if (t[i].kind == T_HASHHASH)
			p->sv[p->svn - 1].flags |= PF_NOPASTE;
	}
	return first;
}

/* substitute arguments into m's body (clang's ExpandFunctionArguments), paste, push */
static uint32_t va_opt(pp *p, const pmacro *m, margs *a, uint32_t i, uint32_t to, size_t base,
		       uint32_t eloc, bool *ngsp, bool str);

/* substitute body[from, to) into sv (clang's ExpandFunctionArguments) */
static void subst_range(pp *p, const pmacro *m, margs *a, uint32_t from, uint32_t to, size_t base,
			uint32_t eloc, bool *ngsp)
{
	const vtok *body = m->body;
	const uint8_t *pidx = m->pidx;
	uint32_t np = m->nparam;
	bool ngs = *ngsp, va = m->flags & MF_VARIADIC;
	ptok t;

	for (uint32_t i = from; i < to; i++) {
		const vtok *cur = &body[i];
		uint32_t k = pidx ? pidx[i] : 0;
		bool before_nonempty, before, after;

		if (i && (cur->flags & TF_SPACE))
			ngs = true;
		if (unlikely(cur->kind == T_IDENT && cur->id == KW_VA_OPT && va)) {
			i = va_opt(p, m, a, i, to, base, eloc, &ngs, false);
			continue;
		}
		if (unlikely(cur->kind == T_HASH && m->kind == MK_FUNC && i + 1 < to &&
			     body[i + 1].kind == T_IDENT && body[i + 1].id == KW_VA_OPT && va)) {
			i = va_opt(p, m, a, i + 1, to, base, eloc, &ngs, true);
			continue;
		}
		if (cur->kind == T_HASH && m->kind == MK_FUNC && i + 1 < to && pidx && pidx[i + 1]) {
			uint32_t j = pidx[i + 1] - 1;

			stringify(p, a->tok + a->st[j], a->ln[j], &t);
			t.loc = eloc;
			if (ngs)
				t.flags |= TF_SPACE;
			svpush(p, &t);
			i++;
			ngs = false;
			continue;
		}
		before_nonempty = p->svn > base && hashhash(&p->sv[p->svn - 1]);
		before = i && body[i - 1].kind == T_HASHHASH;
		after = i + 1 < to && body[i + 1].kind == T_HASHHASH;
		if (!k) {
			vfill(&t, cur, m->src, eloc);
			t.flags &= ~(TF_SOL | TF_SPACE);
			if (ngs) {
				t.flags |= TF_SPACE;
				ngs = false;
			}
			svpush(p, &t);
			continue;
		}
		k--;
		if (!before && !after) {
			const ptok *at = a->pre[k];
			uint32_t an = a->preln[k];

			if (an) {
				size_t f = append(p, at, an);

				p->sv[f].flags = (p->sv[f].flags & ~(TF_SOL | TF_SPACE)) | (ngs ? TF_SPACE : 0);
				ngs = false;
			}
			continue;
		}
		/* operand of ##: the unexpanded argument */
		if (a->ln[k]) {
			bool pseudo = false;
			size_t f;

			if (before_nonempty && p->svn - base >= 2 && p->sv[p->svn - 2].kind == T_COMMA &&
			    va && k == np - 1) {
				pseudo = true;	/* GNU ", ## __VA_ARGS__" with arguments */
				p->svn--;
			}
			f = append(p, a->tok + a->st[k], a->ln[k]);
			/* clang: unexpanded argument tokens take the expansion's location */
			for (size_t j = f; j < p->svn; j++)
				p->sv[j].loc = eloc;
			if (!pseudo)
				p->sv[f].flags = (p->sv[f].flags & ~(TF_SOL | TF_SPACE)) | (ngs ? TF_SPACE : 0);
			ngs = false;
			continue;
		}
		/* empty operand: placemarker */
		if (after) {
			i++;
			continue;
		}
		if (before_nonempty)
			p->svn--;
		if (va && k == np - 1)
			remove_va_comma(p, base, &ngs);
	}
	*ngsp = ngs;
}

/*
 * __VA_OPT__(...) at body[i] (C23 6.10.5.1): the content if the variadic
 * argument is non-empty after expansion, else a placemarker.  With str,
 * the result is stringified (#__VA_OPT__).  Returns the index of its ')'.
 */
static uint32_t va_opt(pp *p, const pmacro *m, margs *a, uint32_t i, uint32_t to, size_t base,
		       uint32_t eloc, bool *ngsp, bool str)
{
	const vtok *body = m->body;
	uint32_t k = m->nparam - 1, b = i + 2, e = b, depth = 0;
	size_t start = p->svn;
	bool before = i && body[i - 1].kind == T_HASHHASH && !str;

	for (; e < to; e++) {
		if (body[e].kind == T_LPAREN)
			depth++;
		else if (body[e].kind == T_RPAREN && !depth--)
			break;
	}
	if (i + 1 >= to || body[i + 1].kind != T_LPAREN || e >= to) {
		error(p, eloc, "__VA_OPT__ must be followed by a parenthesized list");
		return to;
	}
	if (!a->prest[k])
		preexpand(p, a, k);
	if (a->preln[k]) {
		bool ngs = false;

		subst_range(p, m, a, b, e, start, eloc, &ngs);
	}
	if (str) {
		ptok t;
		uint32_t n;
		const ptok *r = svtake(p, start, &n);

		stringify(p, r, n, &t);
		t.loc = eloc;
		if (*ngsp)
			t.flags |= TF_SPACE;
		svpush(p, &t);
	} else if (p->svn == start) {
		/* placemarker: a ## pm ## d is a ## d; pm ## d is d; a ## pm is a */
		if (e + 1 < to && body[e + 1].kind == T_HASHHASH)
			e++;
		else if (before && p->svn > base && hashhash(&p->sv[p->svn - 1]))
			p->svn--;
	} else {
		p->sv[start].flags = (p->sv[start].flags & ~(TF_SOL | TF_SPACE)) | (*ngsp ? TF_SPACE : 0);
	}
	*ngsp = false;
	return e;
}

static int subst(pp *p, const pmacro *m, const ptok *name, margs *a, uint32_t eloc)
{
	const vtok *body = m->body;
	const uint8_t *pidx = m->pidx;
	uint32_t nb = m->nbody, n;
	uint8_t nf = name->flags & (TF_SOL | TF_SPACE);
	bool ngs = false;
	size_t base;
	ptok *r;

	/* pre-expand first: it pushes contexts and uses sv */
	if (a && pidx) {
		for (uint32_t i = 0; i < nb; i++) {
			uint32_t k = pidx[i];

			if (!k || a->prest[k - 1])
				continue;
			if (i && body[i - 1].kind == T_HASH)
				continue;
			if ((i && body[i - 1].kind == T_HASHHASH) || (i + 1 < nb && body[i + 1].kind == T_HASHHASH))
				continue;
			preexpand(p, a, k - 1);
		}
	}
	base = p->svn;
	subst_range(p, m, a, 0, nb, base, eloc, &ngs);
	/* token pasting, left to right */
	if (m->flags & MF_PASTE) {
		size_t w = base, e = p->svn;

		for (size_t i = base; i < e;) {
			ptok cur = p->sv[i++];

			while (i + 1 < e && hashhash(&p->sv[i])) {
				ptok r;

				if (p->o->asm_mode && !paste_ok(&cur, &p->sv[i + 1])) {
					/* assembler: an invalid paste leaves both tokens */
					p->sv[w++] = cur;
					cur = p->sv[i + 1];
					i += 2;
					continue;
				}
				r = paste(p, &cur, &p->sv[i + 1]);
				cur = r;
				i += 2;
			}
			p->sv[w++] = cur;
		}
		p->svn = w;
	}
	r = svtake(p, base, &n);
	if (!n) {
		p->pend |= nf;
		return 0;
	}
	push_ptok(p, r, n, m->name, 0x80 | nf, 0);
	mdisable(p, m->name);
	return 0;
}

/* clang's isTrivialSingleTokenExpansion */
static bool trivial(pp *p, const pmacro *m)
{
	const vtok *v = &m->body[0];
	uintptr_t e;

	if (v->kind != T_IDENT)
		return true;
	e = mread(p, v->id);
	if (e && !(e & 1) && v->id != m->name)
		return false;
	if (m->kind == MK_OBJ)
		return true;
	return !m->pidx || !m->pidx[0];
}

static void single(pp *p, const pmacro *m, ptok *t, uint8_t nf, uint32_t eloc)
{
	uintptr_t e;

	vfill(t, &m->body[0], m->src, eloc);
	t->flags = (t->flags & ~(TF_SOL | TF_SPACE)) | nf;
	if (t->kind == T_IDENT && (e = mread(p, t->id)) && ((e & 1) || (const pmacro *)(e & ~(uintptr_t)1) == m))
		t->flags |= PF_NOEXP;
}

static int builtin(pp *p, const pmacro *m, ptok *t);

/* expand macro m named by t; 1 if t now holds the result token */
static int expand(pp *p, const pmacro *m, ptok *t)
{
	uint8_t nf = t->flags & (TF_SOL | TF_SPACE);
	margs a;
	ptok rp;

	if (m->kind == MK_BUILTIN)
		return builtin(p, m, t);
	if (m->kind == MK_OBJ) {
		ctx *c;

		if (!m->nbody) {
			p->pend |= nf;
			return 0;
		}
		if (m->nbody == 1 && trivial(p, m)) {
			single(p, m, t, nf, t->loc);
			return 1;
		}
		if (m->flags & MF_PASTE)
			return subst(p, m, t, NULL, t->loc);
		c = push(p);
		c->type = C_VTOK;
		c->vt = m->body;
		c->n = m->nbody;
		c->src = m->src;
		c->loc = t->loc;
		c->mid = m->name;
		c->first = 0x80 | nf;
		mdisable(p, m->name);
		return 0;
	}
	if (!collect(p, m, t, &a, &rp))
		return 1;
	if (!m->nbody) {
		p->pend |= nf;
		return 0;
	}
	if (m->nbody == 1 && trivial(p, m)) {
		single(p, m, t, nf, rp.loc);
		return 1;
	}
	return subst(p, m, t, &a, rp.loc);
}

/* ---------------- builtin macros ---------------- */

static const ctx *cur_file(const pp *p)
{
	for (int d = p->depth - 1; d >= 0; d--)
		if (p->cx[d].type == C_FILE)
			return &p->cx[d];
	return NULL;
}

static void mknum(pp *p, ptok *t, unsigned long long v)
{
	char *b = alloc(&p->tmp, 40);
	int n = utoa(b, v);

	memset(b + n, 0, 16);
	t->s = b;
	t->len = (uint32_t)n;
	t->kind = T_NUMBER;
	t->id = 0;
	t->ctr = 0;
	t->flags &= ~(TF_DIRTY | TF_RAW | PF_NOEXP);
}

static void mkstr(pp *p, ptok *t, const char *s, size_t n)
{
	char *b = alloc(&p->tmp, 2 * n + 24), *w = b;

	*w++ = '"';
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '\\' || s[i] == '"')
			*w++ = '\\';
		*w++ = s[i];
	}
	*w++ = '"';
	memset(w, 0, 16);
	t->s = b;
	t->len = (uint32_t)(w - b);
	t->kind = T_STRING;
	t->id = 0;
	t->ctr = 0;
	t->flags &= ~(TF_DIRTY | TF_RAW | PF_NOEXP);
}

/* put t back: it is read again next */
static void unread(pp *p, const ptok *t)
{
	ptok *c = alloc(&p->tmp, sizeof(*c));

	*c = *t;
	push_ptok(p, c, 1, 0, 0, 0);
}

static const vlex_file *find_include(pp *p, const ctx *from, const char *name, size_t nlen,
				     bool angled, bool next, int32_t *sdir, const char **path, const incent **ie);
static void enter_file(pp *p, const vlex_file *f, const char *path, int32_t sdir);

/*
 * A header name after '(' (has_include) or in #include: "x" or <x> as
 * written, or else from macro-expanded tokens.  Returns false if malformed.
 */
static bool header_name(pp *p, char *buf, size_t cap, size_t *len, bool *angled, ptok *t)
{
	size_t n = 0;
	bool expanded = false;

	if (t->kind == T_IDENT) {
		unread(p, t);
		next_exp(p, t);
		expanded = true;
	}
	if (t->kind == T_STRING) {
		size_t l;
		const char *s = pp_spell(t, buf, &l);

		if (l < 2 || l - 2 >= cap)
			return false;
		memmove(buf, s + 1, l - 2);
		*len = l - 2;
		*angled = false;
		return true;
	}
	if (t->kind != T_LESS)
		return false;
	{
		const ptok lt = *t;
		bool contiguous = true;
		const char *prev_end = lt.s + lt.len;

		/* <...> written out is one header-name token: no expansion inside */
		for (;;) {
			if (expanded)
				next_exp(p, t);
			else
				raw(p, t);
			if (t->kind == T_EOF)
				return false;
			if (t->kind == T_GREATER)
				break;
			if (n && (t->flags & (TF_SPACE | TF_SOL)) && n < cap)
				buf[n++] = ' ';
			if (t->s < prev_end || t->s > prev_end + 256)
				contiguous = false;
			prev_end = t->s + t->len;
			if (n + t->len >= cap)
				return false;
			n = (size_t)(spell_to(buf + n, t) - buf);
		}
		/* written in one piece in a file: take the text as is */
		if (contiguous && t->s > lt.s && (size_t)(t->s - lt.s - 1) < cap) {
			n = (size_t)(t->s - lt.s - 1);
			memcpy(buf, lt.s + 1, n);
		}
	}
	*len = n;
	*angled = true;
	return true;
}

/* "(" ... ")" as one spelling, for the feature tests */
static bool feature_arg(pp *p, char *buf, size_t cap, uint32_t loc)
{
	size_t n = 0;
	int depth = 0;
	ptok t;

	raw(p, &t);
	if (t.kind != T_LPAREN) {
		error(p, loc, "missing '(' after feature-test macro");
		return false;
	}
	for (;;) {
		raw(p, &t);
		if (t.kind == T_EOF)
			return false;
		if (t.kind == T_LPAREN)
			depth++;
		if (t.kind == T_RPAREN && !depth--)
			break;
		if (n + t.len + 1 >= cap)
			return false;
		n = (size_t)(spell_to(buf + n, &t) - buf);
	}
	buf[n] = 0;
	return true;
}

static int has_query(pp *p, int b, const char *name, uint32_t loc)
{
	static const char *const q[] = {
		[B_HAS_ATTRIBUTE] = "__has_attribute", [B_HAS_BUILTIN] = "__has_builtin",
		[B_HAS_FEATURE] = "__has_feature", [B_HAS_EXTENSION] = "__has_extension",
		[B_HAS_C_ATTRIBUTE] = "__has_c_attribute", [B_HAS_CPP_ATTRIBUTE] = "__has_cpp_attribute",
		[B_HAS_DECLSPEC] = "__has_declspec_attribute", [B_HAS_WARNING] = "__has_warning",
		[B_HAS_CONSTEXPR_BUILTIN] = "__has_constexpr_builtin",
		[B_IS_TARGET_ARCH] = "__is_target_arch", [B_IS_TARGET_OS] = "__is_target_os",
		[B_IS_TARGET_VENDOR] = "__is_target_vendor",
		[B_IS_TARGET_ENVIRONMENT] = "__is_target_environment",
		[B_IS_TARGET_VARIANT_OS] = "__is_target_variant_os",
		[B_IS_TARGET_VARIANT_ENVIRONMENT] = "__is_target_variant_environment",
		[B_BUILDING_MODULE] = "__building_module",
	};

	size_t lo = 0, hi = sizeof(has_tab) / sizeof(has_tab[0]);

	if (b == B_BUILDING_MODULE)
		return 0;	/* no modules */
	/* the table holds the nonzero answers, sorted by (query, name) */
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int c = strcmp(has_tab[mid].q, q[b]);

		if (!c)
			c = strcmp(has_tab[mid].name, name);
		if (!c)
			return has_tab[mid].v[p->o->target];
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return 0;
}

static int pragma_text(pp *p, const char *s, size_t n, uint32_t loc, ptok *out);

static int builtin(pp *p, const pmacro *m, ptok *t)
{
	uint8_t nf = t->flags & (TF_SOL | TF_SPACE);
	const ctx *fc = cur_file(p);
	char buf[4096];

	switch (m->builtin) {
	case B_LINE:
		mknum(p, t, loc_line(p, t->loc));
		break;
	case B_FILE: {
		uint32_t l, loc = t->loc;
		const char *s;

		if (loc < p->fe[fc->fe].base || loc > p->fe[fc->fe].base + p->fe[fc->fe].f->size)
			loc = p->fe[fc->fe].base + fc->vt[fc->i ? fc->i - 1 : 0].off;
		s = presumed_name(p, fc->fe, loc, &l);
		mkstr(p, t, s, l);
		break;
	}
	case B_FILE_NAME: {
		const fent *e = &p->fe[fc->fe];
		const char *s = e->name + e->namelen;

		while (s > e->name && s[-1] != '/')
			s--;
		mkstr(p, t, s, e->namelen - (size_t)(s - e->name));
		break;
	}
	case B_BASE_FILE:
		HC_BAD(p);
		mkstr(p, t, p->base_name, p->base_namelen);
		break;
	case B_COUNTER:
		if (p->rec.on) {
			if (p->in_dir)
				HC_BAD(p);	/* the value decides something */
			mknum(p, t, p->counter);
			t->ctr = new_cprov(p, p->counter, 0, t->len, 0);
			p->counter++;
		} else {
			mknum(p, t, p->counter++);
		}
		break;
	case B_INCLUDE_LEVEL:
		for (int i = 0; i < p->rec.n; i++)
			p->rec.lev[i].uses_level = 1;
		mknum(p, t, p->nfile > 1 ? (unsigned)p->nfile - 1 : 0);
		break;
	case B_DATE: case B_TIME: case B_TIMESTAMP: {
		time_t now = time(NULL);

		HC_BAD(p);
		struct tm tm;

		localtime_r(&now, &tm);
		strftime(buf, sizeof(buf), m->builtin == B_DATE ? "%b %e %Y" :
			 m->builtin == B_TIME ? "%H:%M:%S" : "%a %b %e %H:%M:%S %Y", &tm);
		mkstr(p, t, buf, strlen(buf));
		break;
	}
	case B_PRAGMA: {
		ptok s, r;
		size_t l;
		const char *sp;
		char *d;
		size_t dn = 0;

		if (p->preexp)
			return 1;	/* runs when the result is rescanned */
		next_exp(p, &r);
		next_exp(p, &s);
		if (r.kind != T_LPAREN || !is_strlit(s.kind)) {
			error(p, t->loc, "_Pragma takes a parenthesized string literal");
			return 1;
		}
		if (s.ctr)
			HC_BAD(p);
		next_exp(p, &r);
		if (r.kind != T_RPAREN)
			error(p, t->loc, "_Pragma takes a parenthesized string literal");
		{
			char tmp[s.len + 1];

			sp = pp_spell(&s, tmp, &l);
			while (l && *sp != '"') {	/* encoding prefix */
				sp++;
				l--;
			}
			d = alloc(&p->tmp, l + 16);
			memcpy(d, "pragma ", 7);
			dn = 7;
			for (size_t i = 1; i + 1 < l; i++) {
				if (sp[i] == '\\' && (sp[i + 1] == '\\' || sp[i + 1] == '"'))
					i++;
				d[dn++] = sp[i];
			}
		}
		if (pragma_text(p, d, dn, t->loc, t)) {
			t->flags = TF_SOL;
			return 1;
		}
		return 0;
	}
	case B_HAS_INCLUDE: case B_HAS_INCLUDE_NEXT: {
		ptok r;
		size_t n;
		bool angled;
		int32_t sd;
		const char *path;

		raw(p, &r);
		if (r.kind != T_LPAREN) {
			error(p, t->loc, "missing '(' after __has_include");
			return 1;
		}
		raw(p, &r);
		if (!header_name(p, buf, sizeof(buf), &n, &angled, &r)) {
			error(p, t->loc, "invalid __has_include argument");
			return 1;
		}
		raw(p, &r);
		if (r.kind != T_RPAREN)
			error(p, t->loc, "missing ')' after __has_include");
		mknum(p, t, find_include(p, fc, buf, n, angled, m->builtin == B_HAS_INCLUDE_NEXT, &sd, &path, NULL) != NULL);
		break;
	}
	case B_IS_IDENTIFIER:
		if (!feature_arg(p, buf, sizeof(buf), t->loc))
			return 1;
		mknum(p, t, 1);
		break;
	default:
		if (!feature_arg(p, buf, sizeof(buf), t->loc)) {
			error(p, t->loc, "malformed feature test");
			return 1;
		}
		mknum(p, t, (unsigned long long)has_query(p, m->builtin, buf, t->loc));
		break;
	}
	t->flags = (t->flags & ~(TF_SOL | TF_SPACE)) | nf;
	return 1;
}

/* ---------------- #if expressions ---------------- */

typedef struct ival {
	uint64_t v;
	bool u;
} ival;

static ival parse_number(pp *p, const ptok *t)
{
	char tmp[t->len + 1];
	size_t n, i = 0;
	const char *s = pp_spell(t, tmp, &n);
	unsigned base = 10;
	uint64_t v = 0;
	bool over = false, u = false;
	ival r;

	if (n > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		i = 2;
	} else if (n > 1 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
		base = 2;
		i = 2;
	} else if (s[0] == '0') {
		base = 8;
	}
	for (; i < n; i++) {
		unsigned d;
		char c = s[i];

		if (c == '\'')
			continue;
		if (c >= '0' && c <= '9')
			d = (unsigned)(c - '0');
		else if (base == 16 && c >= 'a' && c <= 'f')
			d = (unsigned)(c - 'a' + 10);
		else if (base == 16 && c >= 'A' && c <= 'F')
			d = (unsigned)(c - 'A' + 10);
		else
			break;
		if (d >= base)
			break;
		if (v > (UINT64_MAX - d) / base)
			over = true;
		v = v * base + d;
	}
	for (; i < n; i++) {
		char c = s[i];

		if (c == 'u' || c == 'U')
			u = true;
		else if (c != 'l' && c != 'L') {
			error(p, t->loc, "invalid integer constant '%.*s' in preprocessor expression", (int)n, s);
			break;
		}
	}
	if (over)
		error(p, t->loc, "integer literal is too large");
	r.v = v;
	r.u = u || v > INT64_MAX;
	return r;
}

static ival parse_char(pp *p, const ptok *t)
{
	char tmp[t->len + 1];
	size_t n;
	const char *s = pp_spell(t, tmp, &n), *e;
	uint64_t v = 0;
	unsigned cnt = 0;
	bool narrow = t->kind == T_CHAR || t->kind == T_U8CHAR;
	ival r;

	while (*s != '\'') {
		s++;
		n--;
	}
	e = s + n - 1;
	for (s++; s < e; cnt++) {
		uint64_t c;

		if (*s == '\\') {
			s++;
			switch (*s) {
			case 'n': c = '\n'; s++; break;
			case 't': c = '\t'; s++; break;
			case 'r': c = '\r'; s++; break;
			case 'a': c = 7; s++; break;
			case 'b': c = 8; s++; break;
			case 'f': c = 12; s++; break;
			case 'v': c = 11; s++; break;
			case 'e': c = 27; s++; break;
			case 'x':
				c = 0;
				for (s++; s < e && strchr("0123456789abcdefABCDEF", *s); s++)
					c = c * 16 + (unsigned)(*s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10);
				break;
			default:
				if (*s >= '0' && *s <= '7') {
					c = 0;
					for (int k = 0; k < 3 && s < e && *s >= '0' && *s <= '7'; k++, s++)
						c = c * 8 + (unsigned)(*s - '0');
				} else {
					c = (unsigned char)*s++;
				}
			}
		} else {
			c = (unsigned char)*s++;
		}
		v = narrow ? (v << 8) | (c & 0xff) : c;
	}
	if (narrow && cnt == 1 && t->kind == T_CHAR && !p->o->unsigned_char)
		v = (uint64_t)(int64_t)(int8_t)v;
	else if (narrow && cnt > 1)
		v = (uint64_t)(int64_t)(int32_t)v;
	r.v = v;
	r.u = false;
	return r;
}

typedef struct ev {
	pp *p;
	ptok t;		/* lookahead */
	uint32_t loc;
	bool bad;
} ev;

static void eadv(ev *e)
{
	next_exp(e->p, &e->t);
}

static ival ecomma(ev *e, bool live);

static ival eprimary(ev *e, bool live)
{
	ival r = { 0, false };
	pp *p = e->p;

	switch (e->t.kind) {
	case T_NUMBER:
		r = parse_number(p, &e->t);
		eadv(e);
		return r;
	case T_CHAR: case T_WCHAR: case T_U8CHAR: case T_U16CHAR: case T_U32CHAR:
		r = parse_char(p, &e->t);
		eadv(e);
		return r;
	case T_LPAREN:
		eadv(e);
		r = ecomma(e, live);
		if (e->t.kind != T_RPAREN) {
			if (!e->bad)
				error(p, e->loc, "expected ')' in preprocessor expression");
			e->bad = true;
			return r;
		}
		eadv(e);
		return r;
	case T_PLUS:
		eadv(e);
		return eprimary(e, live);
	case T_MINUS:
		eadv(e);
		r = eprimary(e, live);
		r.v = -r.v;
		return r;
	case T_TILDE:
		eadv(e);
		r = eprimary(e, live);
		r.v = ~r.v;
		return r;
	case T_EXCLAIM:
		eadv(e);
		r = eprimary(e, live);
		r.v = !r.v;
		r.u = false;
		return r;
	case T_IDENT:
		if (e->t.id == KW_defined) {
			ptok n;
			bool paren;

			raw(p, &n);
			paren = n.kind == T_LPAREN;
			if (paren)
				raw(p, &n);
			if (n.kind != T_IDENT) {
				error(p, e->loc, "macro name must be an identifier");
				e->bad = true;
				return r;
			}
			r.v = mdefined(p, n.id);
			if (paren) {
				raw(p, &n);
				if (n.kind != T_RPAREN) {
					error(p, e->loc, "missing ')' after 'defined'");
					e->bad = true;
				}
			}
			eadv(e);
			return r;
		}
		eadv(e);	/* an identifier that is not a macro: 0 */
		return r;
	default:
		if (!e->bad)
			error(p, e->loc, "invalid token at start of a preprocessor expression");
		e->bad = true;
		return r;
	}
}

static int prec(int k)
{
	switch (k) {
	case T_STAR: case T_SLASH: case T_PERCENT: return 10;
	case T_PLUS: case T_MINUS: return 9;
	case T_LESSLESS: case T_GREATERGREATER: return 8;
	case T_LESS: case T_GREATER: case T_LESSEQ: case T_GREATEREQ: return 7;
	case T_EQUALEQUAL: case T_EXCLAIMEQ: return 6;
	case T_AMP: return 5;
	case T_CARET: return 4;
	case T_PIPE: return 3;
	case T_AMPAMP: return 2;
	case T_PIPEPIPE: return 1;
	}
	return 0;
}

static ival ebinary(ev *e, int minp, bool live)
{
	ival l = eprimary(e, live);

	for (;;) {
		int op = e->t.kind, pr = prec(op);
		ival r;
		bool u;

		if (!pr || pr < minp || e->bad)
			return l;
		eadv(e);
		if (op == T_AMPAMP) {
			r = ebinary(e, pr + 1, live && l.v);
			l.v = l.v && r.v;
			l.u = false;
			continue;
		}
		if (op == T_PIPEPIPE) {
			r = ebinary(e, pr + 1, live && !l.v);
			l.v = l.v || r.v;
			l.u = false;
			continue;
		}
		r = ebinary(e, pr + 1, live);
		u = l.u || r.u;
		switch (op) {
		case T_STAR: l.v = l.v * r.v; break;
		case T_SLASH: case T_PERCENT:
			if (!r.v) {
				if (live) {
					error(e->p, e->loc, "division by zero in preprocessor expression");
					e->bad = true;
				}
				l.v = 0;
				break;
			}
			if (u)
				l.v = op == T_SLASH ? l.v / r.v : l.v % r.v;
			else if ((int64_t)r.v == -1)
				l.v = op == T_SLASH ? -l.v : 0;
			else
				l.v = op == T_SLASH ? (uint64_t)((int64_t)l.v / (int64_t)r.v)
						    : (uint64_t)((int64_t)l.v % (int64_t)r.v);
			break;
		case T_PLUS: l.v = l.v + r.v; break;
		case T_MINUS: l.v = l.v - r.v; break;
		case T_LESSLESS:
			l.v = r.v >= 64 ? 0 : l.v << r.v;
			u = l.u;
			break;
		case T_GREATERGREATER:
			if (r.v >= 64)
				l.v = l.u || (int64_t)l.v >= 0 ? 0 : ~0ull;
			else
				l.v = l.u ? l.v >> r.v : (uint64_t)((int64_t)l.v >> r.v);
			u = l.u;
			break;
		case T_LESS: l.v = u ? l.v < r.v : (int64_t)l.v < (int64_t)r.v; u = false; break;
		case T_GREATER: l.v = u ? l.v > r.v : (int64_t)l.v > (int64_t)r.v; u = false; break;
		case T_LESSEQ: l.v = u ? l.v <= r.v : (int64_t)l.v <= (int64_t)r.v; u = false; break;
		case T_GREATEREQ: l.v = u ? l.v >= r.v : (int64_t)l.v >= (int64_t)r.v; u = false; break;
		case T_EQUALEQUAL: l.v = l.v == r.v; u = false; break;
		case T_EXCLAIMEQ: l.v = l.v != r.v; u = false; break;
		case T_AMP: l.v &= r.v; break;
		case T_CARET: l.v ^= r.v; break;
		case T_PIPE: l.v |= r.v; break;
		}
		l.u = u;
	}
}

static ival econd(ev *e, bool live)
{
	ival c = ebinary(e, 1, live), a, b;

	if (e->t.kind != T_QUESTION || e->bad)
		return c;
	eadv(e);
	a = ecomma(e, live && c.v);
	if (e->t.kind != T_COLON) {
		if (!e->bad)
			error(e->p, e->loc, "expected ':' in preprocessor expression");
		e->bad = true;
		return a;
	}
	eadv(e);
	b = econd(e, live && !c.v);
	a = c.v ? a : b;
	a.u = a.u || b.u;
	return a;
}

static ival ecomma(ev *e, bool live)
{
	ival r = econd(e, live);

	while (e->t.kind == T_COMMA && !e->bad) {
		eadv(e);
		r = econd(e, live);
	}
	return r;
}

/* evaluate tokens [b, e) of fc's current file as an #if expression */
static bool eval_if(pp *p, const ctx *fc, uint32_t b, uint32_t e)
{
	int d = p->depth;
	ev E = { .p = p, .loc = fc->loc + fc->vt[b > 0 ? b - 1 : 0].off };
	ival r;

	if (b >= e) {
		error(p, E.loc, "#if with no expression");
		return false;
	}
	push_line(p, fc, b, e);
	p->in_dir++;
	eadv(&E);
	r = ecomma(&E, true);
	p->in_dir--;
	if (!E.bad && E.t.kind != T_EOF)
		error(p, E.loc, "token is not a valid binary operator in a preprocessor subexpression");
	pop_to(p, d);
	return r.v != 0;
}

/* ---------------- directives ---------------- */

static bool cond_true(pp *p, const ctx *c, const pdir *d)
{
	uint32_t b = d->tok + 2;

	switch (d->kind) {
	case D_IFDEF: case D_IFNDEF: case D_ELIFDEF: case D_ELIFNDEF: {
		bool def;

		if (b >= d->end || c->vt[b].kind != T_IDENT) {
			error(p, c->loc + c->vt[d->tok].off, "macro name missing");
			return false;
		}
		def = mdefined(p, c->vt[b].id);
		return (d->kind == D_IFDEF || d->kind == D_ELIFDEF) ? def : !def;
	}
	default:
		return eval_if(p, c, b, d->end);
	}
}

/* in a false group: run on from directive k to the group that is taken */
static void skip_from(pp *p, ctx *c, uint32_t k)
{
	const pdir *dir = c->fi->dir;

	for (;;) {
		const pdir *d = &dir[k];

		if ((d->kind == D_ELIF || d->kind == D_ELIFDEF || d->kind == D_ELIFNDEF) && !cond_true(p, c, d)) {
			k = d->next;
			continue;
		}
		c->i = d->end;
		c->di = k + 1;
		return;
	}
}

/*
 * Parse "#define" tokens vt[b, e) into *m (pidx in memory from mem).
 * Returns 0, or an error message (*eoff: the token it is at).
 */
static const char *parse_define(const vtok *vt, uint32_t b, uint32_t e, pmacro *m,
				void *(*mem)(void *, size_t), void *mc, uint32_t *eoff)
{
	uint32_t params[256], np = 0, i = b + 1;
	uint8_t *pidx = NULL;
	bool va = false;

	memset(m, 0, sizeof(*m));
	m->name = vt[b].id;
	m->kind = MK_OBJ;
	*eoff = b;
	if (i < e && vt[i].kind == T_LPAREN && !(vt[i].flags & TF_SPACE)) {
		m->kind = MK_FUNC;
		i++;
		if (i < e && vt[i].kind == T_RPAREN) {
			i++;
		} else {
			for (;;) {
				if (i >= e)
					return "invalid macro parameter list";
				if (vt[i].kind == T_ELLIPSIS) {
					params[np++] = KW_VA_ARGS;
					va = true;
					i++;
				} else if (vt[i].kind == T_IDENT && np < 256) {
					params[np++] = vt[i++].id;
					if (i < e && vt[i].kind == T_ELLIPSIS) {
						va = true;	/* GNU named variadic */
						i++;
					}
				} else {
					return "invalid macro parameter list";
				}
				if (i < e && vt[i].kind == T_RPAREN && (i++, 1))
					break;
				if (va || i >= e || vt[i].kind != T_COMMA)
					return "invalid macro parameter list";
				i++;
			}
		}
	}
	m->body = vt + i;
	m->nbody = e - i;
	m->nparam = (uint16_t)np;
	if (va)
		m->flags |= MF_VARIADIC;
	for (uint32_t j = i; j < e; j++) {
		if (vt[j].kind == T_HASHHASH)
			m->flags |= MF_PASTE;
		if (vt[j].kind == T_IDENT && vt[j].id == KW_VA_OPT)
			m->flags |= MF_VAOPT;
	}
	if (m->nbody && (m->body[0].kind == T_HASHHASH || m->body[m->nbody - 1].kind == T_HASHHASH))
		return "'##' cannot appear at either end of a macro expansion";
	if (np) {
		pidx = mem(mc, m->nbody + 1);
		for (uint32_t j = 0; j < m->nbody; j++) {
			pidx[j] = 0;
			if (m->body[j].kind != T_IDENT)
				continue;
			for (uint32_t k = 0; k < np; k++)
				if (params[k] == m->body[j].id) {
					pidx[j] = (uint8_t)(k + 1);
					break;
				}
		}
		m->pidx = pidx;
	}
	if (m->kind == MK_FUNC)
		for (uint32_t j = 0; j < m->nbody; j++)
			if (m->body[j].kind == T_HASH && !(j + 1 < m->nbody && pidx && pidx[j + 1]) &&
			    !(j + 1 < m->nbody && m->body[j + 1].kind == T_IDENT && m->body[j + 1].id == KW_VA_OPT)) {
				m->flags |= MF_BADHASH;
				*eoff = i + j;
				break;
			}
	return NULL;
}

static void *mem_perm(void *a, size_t n) { return alloc(a, n); }
static void *mem_shared(void *a, size_t n) { (void)a; return salloc(n); }

/* ---- content-canonical macros (definitions in the main file and <built-in>) ---- */

#define CANON_BITS 18
static _Atomic(const pmacro *) canon_tab[1u << CANON_BITS];
static const vtok no_body[1];

static uint32_t macro_hash(const pmacro *m)
{
	uint64_t h = m->name * 0x9E3779B97F4A7C15ull ^ ((uint64_t)m->kind << 8 | m->flags | (uint64_t)m->nparam << 16);

	for (uint32_t j = 0; j < m->nbody; j++) {
		const vtok *v = &m->body[j];
		uint32_t x = v->kind | (v->flags & (TF_SPACE | TF_DIRTY)) << 8 | (m->pidx ? m->pidx[j] : 0) << 16;

		h = (h ^ x) * 0x100000001B3ull;
		h = (h ^ vintern_hash(m->src + v->off, v->len)) * 0x100000001B3ull;
	}
	return (uint32_t)(h ^ h >> 32);
}

static bool macro_same(const pmacro *a, const pmacro *b)
{
	if (a->name != b->name || a->kind != b->kind || a->flags != b->flags || a->nparam != b->nparam ||
	    a->nbody != b->nbody || !a->pidx != !b->pidx)
		return false;
	for (uint32_t j = 0; j < a->nbody; j++) {
		const vtok *x = &a->body[j], *y = &b->body[j];

		if (x->kind != y->kind || x->len != y->len || ((x->flags ^ y->flags) & (TF_SPACE | TF_DIRTY)) ||
		    (a->pidx && a->pidx[j] != b->pidx[j]) || memcmp(a->src + x->off, b->src + y->off, x->len))
			return false;
	}
	return true;
}

/* the shared macro equal to m (m's body may be in per-TU memory) */
static const pmacro *canon_macro(const pmacro *m)
{
	uint32_t h = macro_hash(m);
	pmacro *c = NULL;

	for (uint32_t i = h >> (32 - CANON_BITS), k = 0; k < 64; i = (i + 1) & ((1u << CANON_BITS) - 1), k++) {
		const pmacro *e = atomic_load_explicit(&canon_tab[i], memory_order_acquire);

		if (!e) {
			if (!c) {
				/* a copy in shared memory: body tokens rebased on a copy of their text */
				uint32_t lo = m->nbody ? m->body[0].off : 0;
				uint32_t hi = m->nbody ? m->body[m->nbody - 1].off + m->body[m->nbody - 1].len : 0;
				char *t = salloc(hi - lo + 64);
				vtok *v = m->nbody ? salloc(m->nbody * sizeof(vtok)) : (vtok *)no_body;

				memcpy(t, m->src + lo, hi - lo);
				memset(t + (hi - lo), 0, 64);
				for (uint32_t j = 0; j < m->nbody; j++) {
					v[j] = m->body[j];
					v[j].off -= lo;
				}
				c = salloc(sizeof(*c));
				*c = *m;
				c->body = v;
				c->src = t;
				c->hash = h;
				if (m->pidx) {
					uint8_t *x = salloc(m->nbody + 1);

					memcpy(x, m->pidx, m->nbody);
					c->pidx = x;
				}
			}
			if (atomic_compare_exchange_strong_explicit(&canon_tab[i], &e, c, memory_order_acq_rel,
								    memory_order_acquire))
				return c;
		}
		if (e->hash == h && macro_same(e, m))
			return e;
	}
	return NULL;	/* table crowded here: keep the per-TU one */
}

static void mdef(pp *p, uint32_t id, const pmacro *m);

static void define(pp *p, ctx *c, uint32_t b, uint32_t e)
{
	const vtok *vt = c->vt;
	const pmacro *m;
	const char *err;
	uint32_t eoff;
	bool site = c->f != &p->builtin && c->fe != 0;	/* a header: one macro per directive */
	_Atomic(const pmacro *) *slot = site ? &((pfile *)c->fi)->mac[c->di - 1] : NULL;

	if (b >= e || vt[b].kind != T_IDENT) {
		error(p, c->loc + vt[b - 1].off, "macro name must be an identifier");
		return;
	}
	if (vt[b].id == KW_defined) {
		error(p, c->loc + vt[b].off, "'defined' cannot be used as a macro name");
		return;
	}
	m = slot ? atomic_load_explicit(slot, memory_order_acquire) : NULL;
	if (!m) {
		pmacro *n = site ? salloc(sizeof(*n)) : alloc(&p->perm, sizeof(*n));

		err = parse_define(vt, b, e, n, site ? mem_shared : mem_perm, &p->perm, &eoff);
		if (err) {
			error(p, c->loc + vt[eoff].off, "%s", err);
			return;
		}
		n->src = c->src;
		if (!n->nbody)
			n->body = no_body;
		if ((n->flags & MF_BADHASH) && !p->o->asm_mode) {
			error(p, c->loc + vt[eoff].off, "'#' is not followed by a macro parameter");
			return;
		}
		if (site) {
			const pmacro *z = NULL;

			if (!atomic_compare_exchange_strong_explicit(slot, &z, n, memory_order_acq_rel, memory_order_acquire))
				n = (pmacro *)z;	/* the same definition, built by another TU */
			m = n;
		} else {
			m = canon_macro(n);
			if (!m)
				m = n;
		}
	} else if ((m->flags & MF_BADHASH) && !p->o->asm_mode) {
		/* cached by an assembler TU; body offsets are this file's */
		uint32_t j = 0;

		while (j < m->nbody && !(m->body[j].kind == T_HASH && !(j + 1 < m->nbody && m->pidx && m->pidx[j + 1]) &&
					 !(j + 1 < m->nbody && m->body[j + 1].kind == T_IDENT && m->body[j + 1].id == KW_VA_OPT)))
			j++;
		error(p, c->loc + (j < m->nbody ? m->body[j].off : vt[b].off), "'#' is not followed by a macro parameter");
		return;
	}
	mdef(p, m->name, m);
}

static char *pstrdup(pp *p, const char *s, size_t n)
{
	char *d = alloc(&p->perm, n + 1);

	memcpy(d, s, n);
	d[n] = 0;
	return d;
}

unsigned long pp_stat_try, pp_stat_skip;

/* the file at a candidate path, if any: misses in missing directories cost no syscall */
static const vlex_file *try_path(const char *cwd, const char *path, size_t len)
{
	static int off = -1;

	if (off < 0)
		off = getenv("VPP_NODIRCACHE") != NULL;
	pp_stat_try++;
	const vlex_file *f;

	if (!off && (!cwd || path[0] == '/') && !vdir_exists(path, len)) {
		pp_stat_skip++;
		return NULL;
	}
	if (cwd && path[0] != '/') {
		char abs[8192];
		size_t cl = strlen(cwd);

		if (cl + len + 2 > sizeof(abs))
			return NULL;
		memcpy(abs, cwd, cl);
		abs[cl] = '/';
		memcpy(abs + cl + 1, path, len);
		abs[cl + 1 + len] = 0;
		return vlex_get(abs, cl + 1 + len);
	}
	f = vlex_get(path, len);
	return f;
}

static const vlex_file *find_include_(pp *p, const ctx *from, const char *name, size_t nlen,
				      bool angled, bool next, int32_t *sdir, const char **path, uint32_t *used)
{
	char buf[4096];
	const vlex_file *f;
	int32_t start;

	*sdir = -1;
	*used = 0;
	if (nlen + 2 >= sizeof(buf))
		return NULL;
	if (name[0] == '/') {
		memcpy(buf, name, nlen);
		buf[nlen] = 0;
		*path = pstrdup(p, buf, nlen);
		return try_path(p->o->cwd, buf, nlen);
	}
	if (next && from && from->sdir < 0)
		next = false;	/* #include_next outside the search path: a normal search */
	if (!angled && !next) {
		const char *fp = from && from->f != &p->builtin ? from->spath : "";
		const char *sl = strrchr(fp, '/');
		size_t dl = sl ? (size_t)(sl - fp) + 1 : 0;

		if (dl + nlen + 1 < sizeof(buf)) {
			memcpy(buf, fp, dl);
			memcpy(buf + dl, name, nlen);
			buf[dl + nlen] = 0;
			if ((f = try_path(p->o->cwd, buf, dl + nlen))) {
				*path = pstrdup(p, buf, dl + nlen);
				return f;
			}
		}
	}
	start = next ? from->sdir + 1 : angled ? p->o->nquote : 0;
	for (int32_t k = start; k < p->o->ndirs; k++) {
		const char *d = p->o->dirs[k];
		size_t dl = strlen(d), l;

		if (dl + nlen + 2 >= sizeof(buf))
			continue;
		memcpy(buf, d, dl);
		l = dl;
		if (l && buf[l - 1] != '/')
			buf[l++] = '/';
		memcpy(buf + l, name, nlen);
		l += nlen;
		buf[l] = 0;
		if ((f = try_path(p->o->cwd, buf, l))) {
			*sdir = k;
			*path = pstrdup(p, buf, l);
			*used = (uint32_t)k + 1;
			return f;
		}
	}
	*used = (uint32_t)p->o->ndirs;	/* not in any of them */
	/* frameworks: <Fw/h.h> -> DIR/Fw.framework/Headers/h.h */
	if (angled && !next) {
		const char *sl = memchr(name, '/', nlen);

		for (int k = 0; sl && k < p->o->nfwdirs; k++) {
			int l = snprintf(buf, sizeof(buf), "%s/%.*s.framework/Headers/%.*s", p->o->fwdirs[k],
					 (int)(sl - name), name, (int)(nlen - (size_t)(sl - name) - 1), sl + 1);

			if (l > 0 && (size_t)l < sizeof(buf) && (f = try_path(p->o->cwd, buf, (size_t)l))) {
				*path = pstrdup(p, buf, (size_t)l);
				*sdir = -2;
				return f;
			}
		}
	}
	return NULL;
}

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
 */
struct incent {
	uint64_t hash;
	uint32_t sig, klen;
	int32_t mode;		/* start * 4 + angled * 2 + next */
	uint32_t used, usig;	/* directories looked at, psig[used] */
	int32_t sdir;
	const vlex_file *f;
	const char *path;
	char key[];		/* includer's directory, '\0', name; zero padded to 8 */
};

#define INC_BITS 17
static _Atomic(incent *) inc_tab[1u << INC_BITS];
static _Atomic uint32_t inc_n;

static inline bool inc_same(const incent *e, uint64_t h, uint32_t sig, int32_t mode, const char *k, size_t kl)
{
	return e->hash == h && e->sig == sig && e->mode == mode && e->klen == kl && !memcmp(e->key, k, kl);
}

/* e's answer holds for p: the directories it looked at are p's */
static inline bool inc_valid(const pp *p, const incent *e)
{
	return e->used <= (uint32_t)p->o->ndirs && p->psig[e->used] == e->usig;
}

/* the search path prefix the lookup depended on is an input of the open regions */
static inline void inc_used(pp *p, uint32_t used)
{
	if (unlikely(p->rec.on))
		for (int i = 0; i < p->rec.n; i++)
			if (p->rec.lev[i].ndirs < used)
				p->rec.lev[i].ndirs = used;
}

/* the lookup's mode and, for a quoted one, the includer's directory (*fp, *dl) */
static inline int32_t inc_mode(const pp *p, const ctx *from, bool angled, bool next, const char **fp, size_t *dl)
{
	bool nx = next && from->sdir >= 0;
	int32_t start = nx ? from->sdir + 1 : angled ? p->o->nquote : 0;

	*fp = "", *dl = 0;
	if (!angled && !nx && from->f != &p->builtin)
		*fp = from->spath, *dl = from->sdl;
	return start * 4 + angled * 2 + nx;
}

/* *ie: the cache entry of the answer, if it has one */
static const vlex_file *find_include(pp *p, const ctx *from, const char *name, size_t nlen,
				     bool angled, bool next, int32_t *sdir, const char **path, const incent **ie)
{
	uint32_t used, i, sig = p->sig;
	const vlex_file *f;
	char kb[4096 + 16];
	const char *fp;
	size_t dl, kl, kp;
	int32_t mode;
	incent *e;
	uint64_t h, w;

	if (ie)
		*ie = NULL;
	if (name[0] == '/' || sig == HNONE || !from || nlen >= 2048)
		goto slow;
	mode = inc_mode(p, from, angled, next, &fp, &dl);
	if (dl >= 2048)
		goto slow;
	memcpy(kb, fp, dl);
	kb[dl] = 0;
	memcpy(kb + dl + 1, name, nlen);
	kl = dl + 1 + nlen;
	kp = (kl + 8) & ~(size_t)7;
	memset(kb + kl, 0, 8);
	h = (uint64_t)sig << 32 ^ ((uint64_t)(uint32_t)mode << 17) ^ kl;
	for (size_t j = 0; j < kp; j += 8) {
		memcpy(&w, kb + j, 8);
		h = (h ^ w) * 0x9E3779B97F4A7C15ull;
		h ^= h >> 29;
	}
	for (i = (uint32_t)(h >> (64 - INC_BITS));; i = (i + 1) & ((1u << INC_BITS) - 1)) {
		e = atomic_load_explicit(&inc_tab[i], memory_order_acquire);
		if (!e)
			break;
		if (inc_same(e, h, sig, mode, kb, kl) && inc_valid(p, e))
			goto hit;
	}
	f = find_include_(p, from, name, nlen, angled, next, sdir, path, &used);
	if (p->psig[used] == HNONE || atomic_fetch_add(&inc_n, 1) >= (3u << INC_BITS) / 4)
		goto done;
	{
		size_t pl = f ? strlen(*path) : 0;
		incent *m = salloc(sizeof(*m) + kp + pl + 1);

		m->hash = h, m->sig = sig, m->klen = (uint32_t)kl, m->mode = mode;
		m->used = used, m->usig = p->psig[used], m->sdir = *sdir, m->f = f;
		memcpy(m->key, kb, kp);
		m->path = NULL;
		if (f) {
			memcpy(m->key + kp, *path, pl + 1);
			m->path = m->key + kp;
		}
		for (;; i = (i + 1) & ((1u << INC_BITS) - 1)) {
			e = NULL;
			if (atomic_compare_exchange_strong_explicit(&inc_tab[i], &e, m, memory_order_acq_rel,
								    memory_order_acquire)) {
				e = m;
				break;
			}
			if (inc_same(e, h, sig, mode, kb, kl) && inc_valid(p, e))
				break;	/* another thread's: the same answer */
		}
	}
hit:
	f = e->f, *sdir = e->sdir, *path = e->path, used = e->used;
	if (ie)
		*ie = e;
	goto done;
slow:
	f = find_include_(p, from, name, nlen, angled, next, sdir, path, &used);
done:
	inc_used(p, used);
	return f;
}

static void add_fent(pp *p, ctx *c, const vlex_file *f, const char *name)
{
	size_t nl = strlen(name);
	const char *from = p->o->prefix_from;
	fent *e = &p->fe[p->nfe];

	e->spath = name;
	if (from && !strncmp(name, from, strlen(from))) {
		const char *to = p->o->prefix_to ? p->o->prefix_to : "";
		size_t fl = strlen(from), tl = strlen(to);
		char *d = alloc(&p->perm, tl + nl - fl + 1);

		memcpy(d, to, tl);
		memcpy(d + tl, name + fl, nl - fl + 1);
		name = d;
		nl = tl + nl - fl;
	}
	e->base = p->next_base;
	e->f = f;
	e->fi = c->fi;
	e->name = name;
	e->namelen = (uint32_t)nl;
	c->fe = p->nfe++;
	c->loc = e->base;
	p->next_base += f->size + 1;
}

/* ---------------- header cache: regions ---------------- */

#define HC_MAXVAR 64	/* entries per key */
#define HC_MAXORD (1u << 20)
#define HC_MAXSEG (1u << 20)

static int once_index(const pp *p, const vlex_file *f)
{
	for (uint32_t i = 0; i < p->nonce; i++)
		if (p->once[i] == f)
			return (int)i;
	return -1;
}

/* can an #include here be a region? */
static bool hc_top(const pp *p)
{
	return p->hc && p->sig != HNONE && p->rec.n < HC_MAXLEV && p->depth == p->nfile && !p->svn && !p->pend &&
	       !p->preexp && !p->collecting && !p->in_dir;
}

static bool hc_valid(const pp *p, const hent *e)
{
	if (e->level >= 0 && e->level != p->nfile)
		return false;
	if ((int)e->ndirs > p->o->ndirs || p->psig[e->ndirs] != e->psig)
		return false;
	for (uint32_t i = 0; i < e->nrg; i++)
		if ((mget(p, e->rg[i].id) != 0) != (e->rg[i].v != 0))
			return false;
	for (uint32_t i = 0; i < e->nord; i++)
		if ((once_index(p, e->ord[i].f) >= 0) != (bool)e->ord[i].present)
			return false;
	for (uint32_t i = 0; i < e->nru; i++)
		if (mget(p, e->ru[i]))
			return false;
	for (uint32_t i = 0; i < e->nrv; i++) {
		uintptr_t c = mget(p, e->rv[i].id) & ~(uintptr_t)1, v = e->rv[i].v;

		if (v == HDEF ? !c : c != v)
			return false;
	}
	for (uint32_t i = 0; i < e->ncrd; i++) {
		const hcread *r = &e->crd[i];
		char b[4096 + 40];
		int dl = utoa(b + r->off, p->counter + r->rel);
		size_t n = r->off + (size_t)dl + (r->len - r->off - r->dlen);
		uint32_t id;

		memcpy(b, r->s, r->off);
		memcpy(b + r->off + dl, r->s + r->off + r->dlen, r->len - r->off - r->dlen);
		memset(b + n, 0, 16);
		id = vintern_find(b, n, vintern_hash(b, n));
		if (id && mget(p, id))
			return false;
	}
	return true;
}

/* an entry's text, children spliced in, __COUNTER__ values from base */
static void emit(pp *p, const hent *e, unsigned base)
{
	uint32_t pos = 0, ci = 0;

	for (uint32_t si = 0; si <= e->nseg; si++) {
		uint32_t to = si < e->nseg ? e->seg[si].at : (uint32_t)e->ntext;

		while (ci < e->nctr && e->ctr[ci].off < to) {
			const hctr *c = &e->ctr[ci++];

			out_put(p, e->text + pos, c->off - pos);
			out_reserve(p, 24);
			p->nout += (size_t)utoa(p->out + p->nout, base + c->rel);
			pos = c->off + c->len;
		}
		out_put(p, e->text + pos, to - pos);
		pos = to;
		if (si < e->nseg)
			emit(p, e->seg[si].child, base + e->seg[si].cdelta);
	}
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

/* ---------------- header cache: token entries (pp_next) ---------------- */

static void push_replay(pp *p, const hent *e, uint32_t base, unsigned cbase, uint8_t top)
{
	ctx *c = push(p);

	c->type = C_REPLAY;
	c->pt = e->tk;
	c->n = (uint32_t)e->ntext;
	c->loc = base;
	c->he = e;
	c->si = c->ci = 0;
	c->cbase = cbase;
	c->top = top;
}

/* a child entry's tokens go before the next own token: pushed */
static bool replay_seg(pp *p, ctx *c)
{
	const hent *e = c->he;
	const hseg *g;

	if (c->si >= e->nseg || e->seg[c->si].at != c->i)
		return false;
	g = &e->seg[c->si++];
	push_replay(p, g->child, c->loc + g->lbase, c->cbase + g->cdelta, 0);
	return true;
}

static void replay_tok(pp *p, ctx *c, ptok *t)
{
	const hent *e = c->he;

	*t = c->pt[c->i];
	t->loc = t->loc ? c->loc + t->loc - 1 : 0;
	t->ctr = 0;
	if (t->kind == T_IDENT)
		t->flags |= PF_NOEXP;	/* handed out as recorded: expanded already */
	if (unlikely(c->ci < e->nctr && e->ctr[c->ci].off == c->i)) {
		/* __COUNTER__ digits in the spelling: this TU's values */
		char *b = alloc(&p->perm, t->len + 24 * 4 + 32), *w = b;
		uint32_t at = 0;

		while (c->ci < e->nctr && e->ctr[c->ci].off == c->i) {
			const hctr *h = &e->ctr[c->ci++];
			uint32_t bo = h->len >> 16, bl = h->len & 0xffff;

			memcpy(w, t->s + at, bo - at);
			w += bo - at;
			w += utoa(w, c->cbase + h->rel);
			at = bo + bl;
		}
		memcpy(w, t->s + at, t->len - at);
		w += t->len - at;
		memset(w, 0, 16);
		t->s = b;
		t->len = (uint32_t)(w - b);
		if (t->kind == T_IDENT)
			t->id = vintern_copy(b, t->len);
	}
	c->i++;
}

static inline bool tu_mem(const pp *p, const char *s)
{
	return (s >= p->tmp.base && s < p->tmp.end) || (s >= p->perm.base && s < p->perm.end);
}

#define HC_MAXTK (1u << 24)
#define HC_MAXSB ((size_t)1 << 28)

/* a token pp_next hands out while recording */
static void rec_tok(pp *p, const ptok *t)
{
	struct hrec *r = &p->rec;
	const hlev *L = &r->lev[r->n - 1];
	ptok *d;

	if (t->kind == T_EOF)
		return;
	if (unlikely(r->ntk >= HC_MAXTK || (t->loc && t->loc < L->base0))) {
		HC_BAD(p);
		return;
	}
	d = &r->tk[r->ntk++];
	*d = *t;
	if (tu_mem(p, t->s)) {
		if (r->nsb + t->len + 16 > HC_MAXSB) {
			HC_BAD(p);
			return;
		}
		d->s = r->sb + r->nsb;
		memcpy(r->sb + r->nsb, t->s, t->len);
		memset(r->sb + r->nsb + t->len, 0, 16);
		r->nsb += t->len + 16;
	}
	if (unlikely(t->ctr)) {
		if (t->flags & TF_DIRTY)
			HC_BAD(p);
		for (uint16_t q = t->ctr; q; q = r->cp[q].next) {
			const cprov *c = &r->cp[q];

			if (r->nctr < HC_MAXCP)
				r->ctr[r->nctr++] = (hcpos){ r->ntk - 1, (uint32_t)c->off << 16 | c->len, c->val };
			else
				HC_BAD(p);
		}
	}
}

static void hc_apply(pp *p, const hent *e)
{
	unsigned c0 = p->counter;
	struct hrec *r = &p->rec;
	hlev *L = r->n ? &r->lev[r->n - 1] : NULL;

	p->hc_last = e;

	if (p->tokmode == 2) {
		if (e->has_text) {
			if (L) {
				if (r->nseg < HC_MAXSEG)
					r->seg[r->nseg++] = (hsegr){ r->ntk, r->ntk + e->total, e, c0, p->next_base };
				else
					HC_BAD(p);
			}
			push_replay(p, e, p->next_base, c0, 1);
		}
	} else if (e->has_text) {
		size_t st;

		if (p->col)
			out_ch(p, e->first_sol ? '\n' : ' ');
		if (L)
			text_start(p, e->first_sol);
		out_reserve(p, e->total + 16 * (size_t)e->nctr);
		st = p->nout;
		emit(p, e, c0);
		p->col = e->col_end;
		if (L) {
			if (r->nseg < HC_MAXSEG)
				r->seg[r->nseg++] = (hsegr){ st, p->nout, e, c0, 0 };
			else
				HC_BAD(p);
		}
	}
	if (L) {
		/* the entry's inputs and writes are the enclosing level's too */
		for (int i = 0; i < r->n; i++) {
			if (e->level >= 0)
				r->lev[i].uses_level = 1;
			if (r->lev[i].ndirs < e->ndirs)
				r->lev[i].ndirs = e->ndirs;
		}
		for (uint32_t i = 0; i < e->nrg; i++)
			touch_read(p, e->rg[i].id, e->rg[i].v, HT_G);
		for (uint32_t i = 0; i < e->nru; i++)
			touch_read(p, e->ru[i], 0, 0);
		for (uint32_t i = 0; i < e->nrv; i++)
			touch_read(p, e->rv[i].id, e->rv[i].v, 0);
		for (uint32_t i = 0; i < e->ndl; i++)
			touch_write(p, e->dl[i].id);
		for (uint32_t i = 0; i < e->nord; i++) {
			if (e->ord[i].present && once_index(p, e->ord[i].f) >= (int)L->nonce0)
				continue;	/* marked by this level itself */
			if (r->nord < HC_MAXORD)
				r->ord[r->nord++] = e->ord[i];
			else
				HC_BAD(p);
		}
		for (uint32_t i = 0; i < e->ncrd; i++) {
			const hcread *c = &e->crd[i];

			if (r->ncrd < HC_MAXCRD)
				r->crd[r->ncrd++] = (hcrd){ c->s, c->len, c->off, c->dlen, c0 + c->rel };
			else
				HC_BAD(p);
		}
	}
	for (uint32_t i = 0; i < e->nfe; i++) {
		const hfent *h = &e->fe[i];
		fent *d = &p->fe[p->nfe++];

		d->base = p->next_base + h->rbase;
		d->f = h->f;
		d->fi = h->fi;
		d->name = h->name;
		d->namelen = h->namelen;
		d->spath = h->spath;
		d->sys = h->sys;
	}
	p->next_base += e->span;
	for (uint32_t i = 0; i < e->ndl; i++)
		mset(p, e->dl[i].id, (const pmacro *)e->dl[i].v);
	for (uint32_t i = 0; i < e->noadd && p->nonce < 4096; i++)
		p->once[p->nonce++] = e->oadd[i];
	p->counter += e->counter_delta;
	if (p->tokmode != 2 || !e->has_text)
		p->pend = e->pend_out;	/* (tokens: when the replay is done) */
	atomic_fetch_add_explicit(&hstat.hit, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.bytes_replayed, e->total, memory_order_relaxed);
}

static void hc_why(const pp *p, const hkey *k, const hent *e)
{
	char b[512];
	int n = 0;

	for (uint32_t i = 0; i < e->nrg && !n; i++)
		if ((mget(p, e->rg[i].id) != 0) != (e->rg[i].v != 0)) {
			const vident *d = &vintern_ents[e->rg[i].id];

			n = snprintf(b, sizeof(b), "hc-miss %s: guard %.*s %s\n", k->spath, (int)d->len, d->s,
				     e->rg[i].v ? "def/undef" : "undef/def");
		}
	for (uint32_t i = 0; i < e->nord && !n; i++)
		if ((once_index(p, e->ord[i].f) >= 0) != (bool)e->ord[i].present)
			n = snprintf(b, sizeof(b), "hc-miss %s: once %s\n", k->spath, e->ord[i].f->path);
	for (uint32_t i = 0; i < e->nru && !n; i++)
		if (mget(p, e->ru[i])) {
			const vident *d = &vintern_ents[e->ru[i]];

			n = snprintf(b, sizeof(b), "hc-miss %s: %.*s undef/def\n", k->spath, (int)d->len, d->s);
		}
	for (uint32_t i = 0; i < e->nrv && !n; i++) {
		uintptr_t c = mget(p, e->rv[i].id) & ~(uintptr_t)1, v = e->rv[i].v;

		if (v == HDEF ? !c : c != v) {
			const vident *d = &vintern_ents[e->rv[i].id];

			n = snprintf(b, sizeof(b), "hc-miss %s: %.*s %s/%s\n", k->spath, (int)d->len, d->s,
				     v == HDEF ? "def" : "val", c ? "def" : "undef");
		}
	}
	if (!n)
		n = snprintf(b, sizeof(b), "hc-miss %s: counter name\n", k->spath);
	(void)!write(2, b, (size_t)n);
}

/* replay a matching entry for this #include; true if done */
static bool hc_try(pp *p, hkey *k)
{
	static int dbg = -1;
	const hent *h = atomic_load_explicit(&k->head, memory_order_acquire);

	for (const hent *e = h; e; e = e->next)
		if (hc_valid(p, e)) {
			hc_apply(p, e);
			return true;
		}
	if (dbg < 0)
		dbg = getenv("VPP_HC_DEBUG") != NULL;
	if (dbg && h)
		hc_why(p, k, h);
	return false;
}

static _Thread_local ptok *tk_spare;
static _Thread_local char *sb_spare;

static _Thread_local struct {
	htouch *t;
	honce *ord;
	hcpos *ctr;
	hcrd *crd;
	hsegr *seg;
	cprov *cp;
} rec_spare;

/* the region's file was just entered (top of the stack) */
static void hc_begin(pp *p, hkey *k)
{
	struct hrec *r = &p->rec;
	hlev *L;

	if (!r->t && rec_spare.t) {
		/* this thread's last pp's buffers: counts are reset below, contents need no clearing */
		r->t = rec_spare.t, r->ord = rec_spare.ord, r->ctr = rec_spare.ctr;
		r->crd = rec_spare.crd, r->seg = rec_spare.seg, r->cp = rec_spare.cp;
		rec_spare.t = NULL;
	}
	if (!r->t) {
		r->t = vm(HC_MAXT * sizeof(htouch));
		r->ord = vm(HC_MAXORD * sizeof(honce));
		r->ctr = vm(HC_MAXCP * sizeof(hcpos));
		r->crd = vm(HC_MAXCRD * sizeof(hcrd));
		r->seg = vm(HC_MAXSEG * sizeof(hsegr));
		r->cp = vm((HC_MAXCP + 1) * sizeof(cprov));
	}
	if (p->tokmode == 2 && !r->tk) {
		if (tk_spare) {
			r->tk = tk_spare, r->sb = sb_spare;
			tk_spare = NULL;
		} else {
			r->tk = vm(HC_MAXTK * sizeof(ptok));
			r->sb = vm(HC_MAXSB);
		}
	}
	if (!r->n) {
		r->nt = r->nord = r->nctr = r->ncrd = r->nseg = r->ncp = 0;
		r->ntk = 0;
		r->nsb = 0;
		r->nbad = 0;
	}
	L = &r->lev[r->n++];
	r->on = 1;
	L->k = k;
	L->serial = ++r->serial;
	L->depth = p->depth;
	L->t0 = r->nt;
	L->o0 = r->nord;
	L->c0 = r->nctr;
	L->r0 = r->ncrd;
	L->s0 = r->nseg;
	L->fe0 = p->cx[p->depth - 1].fe;
	L->base0 = p->fe[L->fe0].base;
	L->nonce0 = p->nonce;
	L->nlm0 = p->nlm;
	L->npushed0 = p->npushed;
	L->errors0 = p->errors;
	L->warns0 = p->warns;
	L->counter0 = p->counter;
	L->text_started = p->tokmode == 2;	/* tokens: positions count from here */
	L->text0 = r->ntk;
	L->uses_level = 0;
	L->ndirs = 0;
	L->nfile = p->nfile - 1;	/* before the region's file */
	atomic_fetch_add_explicit(&hstat.miss, 1, memory_order_relaxed);
}

static void *hcopy(const void *s, size_t n)
{
	void *d = n ? salloc(n) : NULL;

	if (n)
		memcpy(d, s, n);
	return d;
}

static char *hstrdup(const char *s, size_t n)
{
	char *d = salloc(n + 1);

	memcpy(d, s, n);
	d[n] = 0;
	return d;
}

/* tokens [L->text0, end) minus the children's: e's own tokens */
static void publish_tok(pp *p, const hlev *L, size_t end, hent *e)
{
	struct hrec *r = &p->rec;
	uint32_t ns = r->nseg - L->s0, nc = r->nctr - L->c0;
	size_t own = end - L->text0, w = 0, at = L->text0, skipped = 0;
	hseg *sg = salloc((ns ? ns : 1) * sizeof(hseg));
	hctr *ct = salloc((nc ? nc : 1) * sizeof(hctr));
	uint32_t ci = L->c0;
	ptok *tk;

	for (uint32_t i = L->s0; i < r->nseg; i++)
		own -= r->seg[i].end - r->seg[i].start;
	tk = salloc((own ? own : 1) * sizeof(ptok));
	for (uint32_t i = L->s0; i <= r->nseg; i++) {
		size_t to = i < r->nseg ? r->seg[i].start : end;

		for (; ci < r->nctr && r->ctr[ci].pos < to; ci++)
			ct[ci - L->c0] = (hctr){ (uint32_t)(r->ctr[ci].pos - L->text0 - skipped), r->ctr[ci].len,
						 r->ctr[ci].val - L->counter0 };
		for (; at < to; at++, w++) {
			ptok t = r->tk[at];

			t.loc = t.loc ? t.loc - L->base0 + 1 : 0;
			t.ctr = 0;
			if (t.s >= r->sb && t.s < r->sb + r->nsb) {
				char *d = salloc(t.len + 16);

				memcpy(d, t.s, t.len);
				memset(d + t.len, 0, 16);
				t.s = d;
			}
			tk[w] = t;
		}
		if (i < r->nseg) {
			sg[i - L->s0] = (hseg){ (uint32_t)w, r->seg[i].counter0 - L->counter0, r->seg[i].child,
						r->seg[i].base0 - L->base0 };
			skipped += r->seg[i].end - r->seg[i].start;
			at = r->seg[i].end;
		}
	}
	e->tk = tk;
	e->ntext = own;
	e->total = end - L->text0;
	e->seg = sg;
	e->nseg = ns;
	e->ctr = ct;
	e->nctr = nc;
	e->has_text = 1;
}

/* the entry for the innermost level, which ends at output offset end */
static hent *publish(pp *p, const hlev *L, size_t end)
{
	struct hrec *r = &p->rec;
	hent *e = salloc(sizeof(*e));
	uint32_t nrg = 0, nru = 0, nrv = 0, ndl = 0, *ru;
	hread *rg, *rv, *dl;
	hfent *fe;

	memset(e, 0, sizeof(*e));
	for (uint32_t i = L->t0; i < r->nt; i++) {
		const htouch *t = &r->t[i];

		if (t->flags & HT_R) {
			if ((t->flags & HT_G) && (t->v == HDEF || !t->v))
				nrg++;
			else if (t->v)
				nrv++;
			else
				nru++;
		}
		ndl += (t->flags & HT_W) != 0;
	}
	rg = salloc((nrg ? nrg : 1) * sizeof(*rg));
	ru = salloc((nru ? nru : 1) * sizeof(*ru));
	rv = salloc((nrv ? nrv : 1) * sizeof(*rv));
	dl = salloc((ndl ? ndl : 1) * sizeof(*dl));
	nrg = nru = nrv = ndl = 0;
	for (uint32_t i = L->t0; i < r->nt; i++) {
		const htouch *t = &r->t[i];

		if (t->flags & HT_R) {
			if ((t->flags & HT_G) && (t->v == HDEF || !t->v))
				rg[nrg++] = (hread){ t->id, 0, t->v };
			else if (t->v)
				rv[nrv++] = (hread){ t->id, 0, t->v };
			else
				ru[nru++] = t->id;
		}
		if (t->flags & HT_W)
			dl[ndl++] = (hread){ t->id, 0, mget(p, t->id) & ~(uintptr_t)1 };
	}
	atomic_fetch_add_explicit(&hstat.nru, nru, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.nrv, nrv, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.ndl, ndl, memory_order_relaxed);
	atomic_fetch_add_explicit(&hstat.nfe, p->nfe - L->fe0, memory_order_relaxed);
	e->rg = rg, e->nrg = nrg;
	e->ru = ru, e->nru = nru;
	e->rv = rv, e->nrv = nrv;
	e->dl = dl, e->ndl = ndl;
	e->ord = hcopy(r->ord + L->o0, (r->nord - L->o0) * sizeof(honce));
	e->nord = r->nord - L->o0;
	e->oadd = hcopy(p->once + L->nonce0, (p->nonce - L->nonce0) * sizeof(*p->once));
	e->noadd = p->nonce - L->nonce0;
	e->nfe = p->nfe - L->fe0;
	fe = salloc((e->nfe ? e->nfe : 1) * sizeof(hfent));
	for (uint32_t i = 0; i < e->nfe; i++) {
		const fent *f = &p->fe[L->fe0 + i];

		fe[i] = (hfent){ f->f, f->fi, hstrdup(f->name, f->namelen), hstrdup(f->spath, strlen(f->spath)),
				 f->namelen, f->base - L->base0, f->sys };
	}
	e->fe = fe;
	e->span = p->next_base - L->base0;
	e->counter_delta = p->counter - L->counter0;
	e->pend_out = p->pend;
	e->level = L->uses_level ? (int32_t)L->nfile : -1;
	e->ndirs = L->ndirs;
	e->psig = p->psig[L->ndirs];
	{
		hcread *cr = salloc((r->ncrd - L->r0 ? r->ncrd - L->r0 : 1) * sizeof(hcread));

		for (uint32_t i = L->r0; i < r->ncrd; i++) {
			const hcrd *c = &r->crd[i];

			cr[i - L->r0] = (hcread){ hstrdup(c->s, c->len), c->len, c->off, c->dlen, c->val - L->counter0 };
		}
		e->crd = cr;
		e->ncrd = r->ncrd - L->r0;
	}
	if (p->tokmode == 2) {
		if (end > L->text0)
			publish_tok(p, L, end, e);
	} else if (L->text_started) {
		/* own text: the output minus the children's texts */
		uint32_t ns = r->nseg - L->s0, nc = r->nctr - L->c0;
		size_t own = end - L->text0;
		char *t;
		hseg *sg = salloc((ns ? ns : 1) * sizeof(hseg));
		hctr *ct = salloc((nc ? nc : 1) * sizeof(hctr));
		size_t w = 0, at = L->text0, skipped = 0;
		uint32_t ci = L->c0;

		for (uint32_t i = L->s0; i < r->nseg; i++)
			own -= r->seg[i].end - r->seg[i].start;
		t = salloc(own + 1);
		for (uint32_t i = L->s0; i <= r->nseg; i++) {
			size_t to = i < r->nseg ? r->seg[i].start : end;


			for (; ci < r->nctr && r->ctr[ci].pos < to; ci++)
				ct[ci - L->c0] = (hctr){ (uint32_t)(r->ctr[ci].pos - L->text0 - skipped), r->ctr[ci].len,
							 r->ctr[ci].val - L->counter0 };
			memcpy(t + w, p->out + at, to - at);
			w += to - at;
			if (i < r->nseg) {
				sg[i - L->s0] = (hseg){ (uint32_t)w, r->seg[i].counter0 - L->counter0, r->seg[i].child, 0 };
				skipped += r->seg[i].end - r->seg[i].start;
				at = r->seg[i].end;
			}
		}
		e->text = t;
		e->ntext = own;
		e->total = end - L->text0;
		e->seg = sg;
		e->nseg = ns;
		e->ctr = ct;
		e->nctr = nc;
		e->has_text = 1;
		e->first_sol = L->first_sol;
		e->col_end = p->col != 0;
	}
	return e;
}

/* the innermost region's file context is being popped */
static void hc_end(pp *p)
{
	struct hrec *r = &p->rec;
	hlev *L = &r->lev[r->n - 1], *P;
	hkey *k = L->k;
	hent *e = NULL;
	size_t sz;

	if (r->n - 1 < r->nbad || p->errors != L->errors0 || p->warns != L->warns0 || p->nlm != L->nlm0 ||
	    p->npushed != L->npushed0 || p->nonce < L->nonce0) {
		if (r->n - 1 >= r->nbad)
			r->nbad = r->n;	/* the enclosing regions saw it too */
		atomic_fetch_add_explicit(&hstat.uncacheable, 1, memory_order_relaxed);
		if (getenv("VPP_HC_DEBUG")) {
			char b[512];
			int n = snprintf(b, sizeof(b), "hc-bad %s: line %d errors %d warns %d nlm %u\n", k->spath,
					 r->badline, p->errors - L->errors0, p->warns - L->warns0, p->nlm - L->nlm0);

			(void)!write(2, b, (size_t)n);
		}
	} else {
		uint64_t m0 = atomic_load_explicit(&shared_used, memory_order_relaxed);

		sz = (r->nt - L->t0) * 16 + (p->nfe - L->fe0) * (sizeof(hfent) + 64) + sizeof(hent);
		if (atomic_load_explicit(&k->n, memory_order_relaxed) >= HC_MAXVAR ||
		    (hc_mem_max && atomic_load_explicit(&hstat.mem, memory_order_relaxed) + sz > hc_mem_max)) {
			atomic_fetch_add_explicit(&hstat.full, 1, memory_order_relaxed);
		} else {
			e = publish(p, L, p->tokmode == 2 ? r->ntk : p->nout);
			/* (other threads allocate too: close enough for a limit) */
			atomic_fetch_add_explicit(&hstat.mem, atomic_load_explicit(&shared_used, memory_order_relaxed) - m0,
						  memory_order_relaxed);
			atomic_fetch_add_explicit(&hstat.rec, 1, memory_order_relaxed);
			atomic_fetch_add_explicit(&hstat.bytes_rec, e->ntext, memory_order_relaxed);
			atomic_fetch_add_explicit(&k->n, 1, memory_order_relaxed);
			e->next = atomic_load_explicit(&k->head, memory_order_relaxed);
			while (!atomic_compare_exchange_weak_explicit(&k->head, &e->next, e, memory_order_release,
								      memory_order_relaxed))
				;
		}
	}
	if (r->n == 1 && p->top_rec) {
		top_event(p, e, 0, 0, 1, 0);
		p->top_rec = 0;
	}
	r->n--;
	if (r->nbad > r->n)
		r->nbad = r->n;
	if (!r->n) {
		r->on = 0;
		return;
	}
	/* merge into the enclosing level */
	P = &r->lev[r->n - 1];
	{
		uint32_t w = L->t0;

		for (uint32_t i = L->t0; i < r->nt; i++) {
			htouch T = r->t[i];
			uint64_t *st = rslot(p, T.id);

			if ((uint32_t)(T.prev >> 32) == P->serial) {
				htouch *pt = &r->t[(uint32_t)T.prev];

				if (!(pt->flags & HT_W) && (T.flags & HT_R) && (pt->flags & HT_R) && pt->v == HDEF &&
				    T.v != HDEF)
					pt->v = T.v;
				pt->flags |= T.flags & HT_W;
				*st = T.prev;
			} else {
				r->t[w] = T;
				*st = (uint64_t)P->serial << 32 | w;
				w++;
			}
		}
		r->nt = w;
	}
	{
		uint32_t w = L->o0;

		for (uint32_t i = L->o0; i < r->nord; i++)
			if (!r->ord[i].present || once_index(p, r->ord[i].f) < (int)P->nonce0)
				r->ord[w++] = r->ord[i];
		r->nord = w;
	}
	if (e) {
		/* the level's own counter digits and children are inside its entry */
		r->nctr = L->c0;
		r->nseg = L->s0;
		if (e->has_text)
			r->seg[r->nseg++] = (hsegr){ L->text0, p->tokmode == 2 ? r->ntk : p->nout, e, L->counter0,
						     L->base0 };
	}
}

static void enter_file(pp *p, const vlex_file *f, const char *path, int32_t sdir)
{
	const pfile *fi = pfile_get(f);
	const ctx *from = cur_file(p);
	hkey *k = NULL;
	uint8_t sys;
	int oi;
	ctx *c;

	if (fi->guard && mguard(p, fi->guard))
		return;
	oi = once_index(p, f);
	if (unlikely(p->rec.on) && p->rec.nord < HC_MAXORD &&
	    (oi < 0 || oi < (int)p->rec.lev[p->rec.n - 1].nonce0))
		p->rec.ord[p->rec.nord++] = (honce){ f, oi >= 0 };
	if (oi >= 0)
		return;
	sys = sdir == -2 || (sdir >= 0 && p->o->nsys && sdir >= p->o->nsys) ||
	      (sdir == -1 && from && p->fe[from->fe].sys);
	if (f != &p->builtin && from && hc_top(p)) {
		int d0 = p->depth;
		uint32_t b0 = p->next_base;
		unsigned c0 = p->counter;

		k = hkey_get(p->sig * 2 + (p->tokmode == 2), f, sdir, 0, path, sys, true);
		/* top-level: included from the main file or <built-in> (-include) */
		bool top = p->tokmode == 2 && (p->nfile == 1 || (p->nfile == 2 && from->f == &p->builtin));

		if (k && hc_try(p, k)) {
			if (top) {
				top_event(p, p->hc_last, b0, c0, 0, 1);
				if (p->depth > d0)
					p->cx[d0].top = 2;	/* its end comes when the replay does */
				else
					top_event(p, p->hc_last, 0, 0, 1, 1);
			}
			return;
		}
		if (k && top) {
			top_event(p, NULL, b0, c0, 0, 0);
			p->top_rec = 1;
		}
	}
	if (fi->bad)
		fatal(p, 0, "%s: unterminated or unbalanced conditional directives", path);
	c = push(p);
	c->type = C_FILE;
	c->vt = f->tok;
	c->n = f->ntok;
	c->src = f->src;
	c->f = f;
	c->fi = fi;
	c->di = 0;
	c->sdir = sdir;
	c->spath = path;
	{
		const char *sl = strrchr(path, '/');

		c->sdl = sl ? (uint32_t)(sl - path) + 1 : 0;
	}
	p->nfile++;
	add_fent(p, c, f, path);
	p->fe[c->fe].sys = sys;
	if (k)
		hc_begin(p, k);
}

static void include(pp *p, ctx *c, uint32_t b, uint32_t e, int kind, uint32_t loc)
{
	char buf[4096];
	size_t n;
	bool angled;
	int32_t sd;
	const char *path;
	const vlex_file *f;
	int d = p->depth;
	ptok t;
	const incent *ie;
	/* "x" or <x> as written: the name is the site's, so is the answer while the options and directory are */
	_Atomic(const incent *) *site = b < e && (c->vt[b].kind == T_STRING || c->vt[b].kind == T_LESS) &&
						p->sig != HNONE
					    ? &((pfile *)c->fi)->inc[c->di - 1] : NULL;

	if (site && (ie = atomic_load_explicit(site, memory_order_acquire))) {
		const char *fp;
		size_t dl;
		int32_t mode = inc_mode(p, c, c->vt[b].kind == T_LESS, kind == D_INCLUDE_NEXT, &fp, &dl);

		if (ie->sig == p->sig && ie->mode == mode && inc_valid(p, ie) && dl < ie->klen && !ie->key[dl] &&
		    !memcmp(ie->key, fp, dl)) {
			inc_used(p, ie->used);
			enter_file(p, ie->f, ie->path, ie->sdir);
			return;
		}
	}
	push_line(p, c, b, e);
	raw(p, &t);
	if (t.kind == T_STRING || t.kind == T_LESS || t.kind == T_IDENT) {
		bool ok;

		p->in_dir++;
		ok = header_name(p, buf, sizeof(buf), &n, &angled, &t);
		p->in_dir--;
		if (!ok) {
			pop_to(p, d);
			error(p, loc, "expected \"FILENAME\" or <FILENAME>");
			return;
		}
	} else {
		pop_to(p, d);
		error(p, loc, "expected \"FILENAME\" or <FILENAME>");
		return;
	}
	pop_to(p, d);
	f = find_include(p, c, buf, n, angled, kind == D_INCLUDE_NEXT, &sd, &path, &ie);
	if (!f)
		fatal(p, loc, "'%.*s' file not found", (int)n, buf);
	if (site && ie)
		atomic_store_explicit(site, ie, memory_order_release);
	enter_file(p, f, path, sd);
}

/* the macro name in ("NAME") of push_macro/pop_macro, or 0 */
static uint32_t pragma_macro_name(const char *s, size_t n)
{
	const char *e = s + n, *q;

	while (s < e && *s != '"')
		s++;
	if (s == e)
		return 0;
	s++;
	q = memchr(s, '"', (size_t)(e - s));
	if (!q || q == s)
		return 0;
	{
		char b[q - s + 17];

		memcpy(b, s, (size_t)(q - s));
		memset(b + (q - s), 0, 17);
		return vintern_copy(b, (size_t)(q - s));
	}
}

static bool word(const char **s, const char *e, const char *w)
{
	const char *q = *s;
	size_t n = strlen(w);

	while (q < e && (*q == ' ' || *q == '\t'))
		q++;
	if ((size_t)(e - q) < n || memcmp(q, w, n) || (q + n < e && idch((unsigned char)q[n])))
		return false;
	*s = q + n;
	return true;
}

/*
 * A pragma, text "pragma ..." (from #pragma or _Pragma).  The ones the
 * preprocessor implements are run and dropped, as clang -E does; the rest
 * become a P_DIRECTIVE token.  Returns 1 if *out was set.
 */
static int pragma_text(pp *p, const char *s, size_t n, uint32_t loc, ptok *out)
{
	const char *q = s + 6, *e = s + n;	/* after "pragma" */

	if (word(&q, e, "once")) {
		const ctx *fc = cur_file(p);

		if (fc && p->nonce < 4096)
			p->once[p->nonce++] = fc->f;
		return 0;
	}
	if (word(&q, e, "push_macro")) {
		uint32_t id = pragma_macro_name(q, (size_t)(e - q));

		HC_BAD(p);
		if (id && p->npushed < 4096) {
			p->pushed[p->npushed].id = id;
			p->pushed[p->npushed++].m = mread(p, id) & ~(uintptr_t)1;
		}
		return 0;
	}
	if (word(&q, e, "pop_macro")) {
		uint32_t id = pragma_macro_name(q, (size_t)(e - q));

		HC_BAD(p);
		for (uint32_t i = p->npushed; i-- > 0;)
			if (p->pushed[i].id == id) {
				mdef(p, id, (const pmacro *)p->pushed[i].m);
				memmove(&p->pushed[i], &p->pushed[i + 1], (p->npushed - i - 1) * sizeof(p->pushed[0]));
				p->npushed--;
				break;
			}
		return 0;
	}
	{
		const char *r = q;

		if (word(&r, e, "GCC") && (word(&r, e, "system_header") || word(&r, e, "poison") ||
					    word(&r, e, "dependency")))
			return 0;
	}
	memset(out, 0, sizeof(*out));
	out->kind = P_DIRECTIVE;
	out->s = s;
	out->len = (uint32_t)n;
	out->loc = loc;
	out->flags = TF_SOL;
	return 1;
}

/* #line N ["file"] (macro-expanded) or # N "file" flags (GNU line marker) */
static void line_directive(pp *p, ctx *c, uint32_t b, uint32_t e, bool expand_line, uint32_t loc)
{
	int d = p->depth;
	const vtok *last = &c->vt[e - 1];
	struct lmark *m;
	ptok n, f;
	ival v;

	push_line(p, c, b, e);
	if (expand_line) {
		p->in_dir++;
		next_exp(p, &n);
		next_exp(p, &f);
		p->in_dir--;
	} else {
		raw(p, &n);
		raw(p, &f);
	}
	if (n.kind != T_NUMBER) {
		pop_to(p, d);
		error(p, loc, "#line directive requires a positive integer argument");
		return;
	}
	v = parse_number(p, &n);
	if (p->nlm == 65536) {
		pop_to(p, d);
		return;
	}
	m = &p->lm[p->nlm++];
	m->fe = c->fe;
	m->off = last->off + last->len;
	m->pline = file_line(c->f, c->fi, m->off) + 1;
	m->line = (uint32_t)v.v;
	m->name = NULL;
	if (f.kind == T_STRING) {
		char tmp[f.len + 1], *w;
		size_t l;
		const char *s = pp_spell(&f, tmp, &l);

		w = alloc(&p->perm, l + 1);
		m->name = w;
		for (size_t i = 1; i + 1 < l; i++) {
			if (s[i] == '\\' && i + 2 < l)
				i++;
			*w++ = s[i];
		}
		m->namelen = (uint32_t)(w - m->name);
	}
	pop_to(p, d);
}

/* run the directive at c->i; 1 if it produced a token in *out */
static int directive(pp *p, ctx *c, ptok *out)
{
	const pdir *d = &c->fi->dir[c->di];
	uint32_t b = d->tok + 1, e = d->end, loc = c->loc + c->vt[d->tok].off;

	c->i = e;
	c->di++;
	switch (d->kind) {
	case D_IF: case D_IFDEF: case D_IFNDEF:
		if (!cond_true(p, c, d))
			skip_from(p, c, d->next);
		return 0;
	case D_ELIF: case D_ELIFDEF: case D_ELIFNDEF: case D_ELSE: {
		/* the end of a group that was taken: on to #endif */
		uint32_t k = d->next;

		while (c->fi->dir[k].kind != D_ENDIF)
			k = c->fi->dir[k].next;
		c->i = c->fi->dir[k].end;
		c->di = k + 1;
		return 0;
	}
	case D_ENDIF: case D_NULL:
		return 0;
	case D_DEFINE:
		define(p, c, b + 1, e);
		return 0;
	case D_UNDEF:
		if (b + 1 < e && c->vt[b + 1].kind == T_IDENT)
			mdef(p, c->vt[b + 1].id, NULL);
		else
			error(p, loc, "macro name must be an identifier");
		return 0;
	case D_INCLUDE: case D_INCLUDE_NEXT: case D_IMPORT:
		include(p, c, b + 1, e, (int)d->kind, loc);
		return 0;
	case D_PRAGMA: {
		const vtok *f = &c->vt[b], *l = &c->vt[e - 1];

		return pragma_text(p, c->src + f->off, l->off + l->len - f->off, loc, out);
	}
	case D_IDENT: {
		const vtok *f = &c->vt[b], *l = &c->vt[e - 1];

		memset(out, 0, sizeof(*out));
		out->kind = P_DIRECTIVE;
		out->s = c->src + f->off;
		out->len = l->off + l->len - f->off;
		out->loc = loc;
		out->flags = TF_SOL;
		return 1;
	}
	case D_ERROR: case D_WARNING: {
		const vtok *f = &c->vt[b], *l = &c->vt[e - 1];

		diag(p, loc, d->kind == D_ERROR ? "error" : "warning", "#%.*s",
		     (int)(l->off + l->len - f->off), c->src + f->off);
		return 0;
	}
	case D_LINEMARK:
		if (p->o->asm_mode)
			goto pass;
		/* fallthrough */
	case D_LINE:
		line_directive(p, c, d->kind == D_LINE ? b + 1 : b, e, d->kind == D_LINE, loc);
		return 0;
	default:
		if (p->o->asm_mode)
			goto pass;
		error(p, loc, "invalid preprocessing directive");
		return 0;
	}
pass: {
	/* assembler: the line as written ("# comment") */
	const vtok *f = &c->vt[b], *l = &c->vt[e - 1];

	memset(out, 0, sizeof(*out));
	out->kind = P_DIRECTIVE;
	out->s = c->src + f->off;
	out->len = l->off + l->len - f->off;
	out->loc = loc;
	out->flags = TF_SOL;
	return 1;
	}
}

/* ---------------- setup ---------------- */

/* the builtin macros: process-wide, so equal in every TU */
static const struct { const char *n; int b; } bi[] = {
	{ "__FILE__", B_FILE }, { "__LINE__", B_LINE }, { "__COUNTER__", B_COUNTER },
	{ "__DATE__", B_DATE }, { "__TIME__", B_TIME }, { "__TIMESTAMP__", B_TIMESTAMP },
	{ "__BASE_FILE__", B_BASE_FILE }, { "__FILE_NAME__", B_FILE_NAME },
	{ "__INCLUDE_LEVEL__", B_INCLUDE_LEVEL }, { "_Pragma", B_PRAGMA },
	{ "__has_attribute", B_HAS_ATTRIBUTE }, { "__has_builtin", B_HAS_BUILTIN },
	{ "__has_feature", B_HAS_FEATURE }, { "__has_extension", B_HAS_EXTENSION },
	{ "__has_c_attribute", B_HAS_C_ATTRIBUTE }, { "__has_cpp_attribute", B_HAS_CPP_ATTRIBUTE },
	{ "__has_declspec_attribute", B_HAS_DECLSPEC }, { "__has_warning", B_HAS_WARNING },
	{ "__is_identifier", B_IS_IDENTIFIER }, { "__has_include", B_HAS_INCLUDE },
	{ "__has_include_next", B_HAS_INCLUDE_NEXT },
	{ "__has_constexpr_builtin", B_HAS_CONSTEXPR_BUILTIN },
	{ "__is_target_arch", B_IS_TARGET_ARCH }, { "__is_target_os", B_IS_TARGET_OS },
	{ "__is_target_vendor", B_IS_TARGET_VENDOR },
	{ "__is_target_environment", B_IS_TARGET_ENVIRONMENT },
	{ "__is_target_variant_os", B_IS_TARGET_VARIANT_OS },
	{ "__is_target_variant_environment", B_IS_TARGET_VARIANT_ENVIRONMENT },
	{ "__building_module", B_BUILDING_MODULE },
};

static const pmacro *bi_macro[sizeof(bi) / sizeof(bi[0])];

static void bi_init(void)
{
	for (size_t i = 0; i < sizeof(bi) / sizeof(bi[0]); i++) {
		pmacro *m = salloc(sizeof(*m));

		memset(m, 0, sizeof(*m));
		m->kind = MK_BUILTIN;
		m->builtin = (uint8_t)bi[i].b;
		m->name = vintern_copy(bi[i].n, strlen(bi[i].n));
		m->body = no_body;
		bi_macro[i] = m;
	}
}

pp *pp_open(const pp_opts *o)
{
	pp *p = vm(sizeof(pp));
	const vlex_file *mf;
	size_t n = strlen(o->predef);
	char *src;
	vtok *tok;
	void *scratch;

	vintern_init();
	p->o = o;
	p->cx = vm(MAXCTX * sizeof(ctx));
	arena_init(&p->perm, 1ull << 30);
	arena_init(&p->tmp, 1ull << 32);
	p->psig = alloc(&p->perm, ((size_t)o->ndirs + 1) * sizeof(uint32_t));
	if (!sig_of(o, &p->sig, p->psig))
		p->sig = HNONE;
	p->sv = vm(SVMAX * sizeof(ptok));
	p->fe = vm((1u << 20) * sizeof(fent));
	p->once = alloc(&p->perm, 4096 * sizeof(*p->once));
	p->pushed = alloc(&p->perm, 4096 * sizeof(*p->pushed));
	p->lm = vm(65536 * sizeof(*p->lm));
	{
		static pthread_once_t once = PTHREAD_ONCE_INIT;

		pthread_once(&once, bi_init);
		for (size_t i = 0; i < sizeof(bi) / sizeof(bi[0]); i++)
			mset(p, bi_macro[i]->name, bi_macro[i]);
	}

	mf = try_path(o->cwd, o->main, strlen(o->main));
	if (!mf) {
		char m[4200];
		int l = snprintf(m, sizeof(m), "vcc: error: %s: cannot read\n", o->main);

		(void)!write(o->errfd ? o->errfd : 2, m, (size_t)(l < (int)sizeof(m) ? l : (int)sizeof(m) - 1));
		pp_close(p);
		return NULL;
	}
	enter_file(p, mf, o->main, -1);
	p->base_name = p->fe[0].name;
	p->base_namelen = p->fe[0].namelen;

	/* "<built-in>": the predefines, -D/-U and -include, on top of the main file */
	src = salloc(n + 129);	/* interned identifiers point into it: immortal */
	memcpy(src, o->predef, n);
	memset(src + n, 0, 129);
	tok = alloc(&p->perm, (n + 2) * sizeof(vtok));
	scratch = vm(vlex_scratch_size(n) + 4096);
	p->builtin.path = "<built-in>";
	p->builtin.src = src;
	p->builtin.size = (uint32_t)n;
	p->builtin.ntok = vlex(src, n, tok, scratch);
	p->builtin.tok = tok;
	munmap(scratch, vlex_scratch_size(n) + 4096);
	enter_file(p, &p->builtin, "<built-in>", -1);
	return p;
}

static inline void next_tok(pp *p, ptok *t)
{
	if (p->depth == p->nfile && !p->svn)
		p->tmp.p = p->tmp.base;	/* nothing above file level refers to tmp */
	next_exp(p, t);
}

/* the header cache's mode: text for pp_text, tokens for pp_next */
static void hc_mode(pp *p, int mode)
{
	static int env = -1;

	if (env < 0) {
		const char *m = getenv("VPP_HC_MAX_MB");

		hc_mem_max = m ? (size_t)atol(m) << 20 : (size_t)8 << 30;
		env = getenv("VPP_NOHC") != NULL;
	}
	p->hc = !env;
	p->tokmode = mode;
}

void pp_next(pp *p, ptok *t)
{
	ctx *c = &p->cx[p->depth - 1];

	/* a cached header's tokens: straight out, unless a child or __COUNTER__ digits come next */
	if (c->type == C_REPLAY && c->i < c->n) {
		const hent *e = c->he;

		if ((c->si >= e->nseg || e->seg[c->si].at != c->i) && (c->ci >= e->nctr || e->ctr[c->ci].off != c->i)) {
			p->ntok_out++;
			*t = c->pt[c->i++];
			t->loc = t->loc ? c->loc + t->loc - 1 : 0;
			if (t->kind == T_IDENT)
				t->flags |= PF_NOEXP;
			if (unlikely(p->rec.on))
				rec_tok(p, t);
			return;
		}
	}
	if (unlikely(!p->tokmode))
		hc_mode(p, 2);
	next_tok(p, t);
	p->ntok_out++;
	if (unlikely(p->rec.on))
		rec_tok(p, t);
}

const pp_topev *pp_top_events(const pp *p, uint32_t *n)
{
	*n = p->ntev;
	return p->tev_full ? NULL : p->tev ? p->tev : (const pp_topev *)"";
}

uint64_t pp_ntok(const pp *p)
{
	return p->ntok_out;
}

uint64_t pp_skipped(const pp *p)
{
	return p->skipped_tok;
}

int pp_skip_top(pp *p)
{
	const pp_topev *s = p->ntev && !p->tev_full ? &p->tev[p->ntev - 1] : NULL;
	int d0 = -1;

	if (!s || s->end || !s->hit || s->pos + 1 != p->ntok_out)
		return 0;
	for (int d = p->depth - 1; d >= 0; d--)
		if (p->cx[d].type == C_REPLAY && p->cx[d].top == 2) {
			d0 = d;
			break;
		}
	if (d0 < 0 || p->cx[d0].he != s->e)
		return 0;
	for (int d = d0 + 1; d < p->depth; d++)
		if (p->cx[d].type != C_REPLAY)
			return 0;
	p->skipped_tok += p->cx[d0].he->total;
	p->pend |= p->cx[d0].he->pend_out;
	p->depth = d0;
	p->ntok_out = s->pos;
	top_event(p, s->e, 0, 0, 1, 1);
	return 1;
}

/* the token as clang -E -P prints it */
static inline void render(pp *p, const ptok *t)
{
	bool sol = t->kind == P_DIRECTIVE || (t->flags & TF_SOL);
	size_t l;
	const char *sp;

	if (p->col) {
		/* assembler-with-cpp: keep the source spacing like clang does, as
		 * gas syntax is space-sensitive ("\\arg", "1:", ".L x") */
		if (sol || !p->o->asm_mode || (t->flags & TF_SPACE))
			out_ch(p, sol ? '\n' : ' ');
		if (sol)
			p->col = 0;
	}
	if (unlikely(p->rec.on) && !p->rec.lev[p->rec.n - 1].text_started)
		text_start(p, sol);
	if (t->kind == P_DIRECTIVE) {
		out_reserve(p, t->len + 2);
		p->out[p->nout++] = '#';
		memcpy(p->out + p->nout, t->s, t->len);
		p->nout += t->len;
		p->out[p->nout++] = '\n';
		p->col = 0;
		return;
	}
	out_reserve(p, t->len);
	sp = pp_spell(t, p->out + p->nout, &l);
	if (sp != p->out + p->nout)
		memcpy(p->out + p->nout, sp, l);
	if (unlikely(t->ctr) && p->rec.on)
		for (uint16_t r = t->ctr; r; r = p->rec.cp[r].next) {
			const cprov *c = &p->rec.cp[r];

			if (p->rec.nctr < HC_MAXCP)
				p->rec.ctr[p->rec.nctr++] = (hcpos){ p->nout + c->off, c->len, c->val };
			else
				HC_BAD(p);
		}
	p->nout += l;
	p->col++;
}

int pp_text(pp *p, const char **out, size_t *len)
{
	ptok t;

	hc_mode(p, 1);
	if (p->o->asm_mode)
		p->hc = 0;	/* cached text is C-spaced */
	for (;;) {
		next_tok(p, &t);
		if (t.kind == T_EOF)
			break;
		render(p, &t);
	}
	if (p->col)
		out_ch(p, '\n');
	*out = p->out ? p->out : "";
	*len = p->nout;
	return p->errors;
}

void pp_hc_stats(char *buf, size_t n)
{
	snprintf(buf, n, "header cache: %lu hits, %lu misses (%lu recorded, %lu uncacheable, %lu not stored), "
		 "%.1f MB replayed, %.1f MB recorded, %.1f MB stored",
		 (unsigned long)hstat.hit, (unsigned long)hstat.miss, (unsigned long)hstat.rec,
		 (unsigned long)hstat.uncacheable, (unsigned long)hstat.full, hstat.bytes_replayed / 1e6,
		 hstat.bytes_rec / 1e6, hstat.mem / 1e6);
	if (getenv("VPP_HC_DEBUG")) {
		size_t l = strlen(buf);

		snprintf(buf + l, n - l, "\n  stored: %lu undef reads, %lu other reads, %lu writes, %lu files",
			 (unsigned long)hstat.nru, (unsigned long)hstat.nrv, (unsigned long)hstat.ndl,
			 (unsigned long)hstat.nfe);
	}
}

int pp_errors(const pp *p)
{
	return p->errors;
}

void pp_set_fatal(pp *p, jmp_buf *jb)
{
	p->fatal_jb = jb;
}

/* the distinct file names in entry order (a hash set: TUs enter ~1000 files) */
size_t pp_files(const pp *p, const char **names, size_t max)
{
	uint32_t cap = 64, stk[1024], *tab;
	size_t n = 0;

	while (cap < 2 * p->nfe)
		cap *= 2;
	tab = cap <= 1024 ? stk : malloc(cap * sizeof(*tab));
	if (!tab)
		abort();
	memset(tab, 0, cap * sizeof(*tab));
	for (uint32_t i = 0; i < p->nfe; i++) {
		const char *s = p->fe[i].spath;
		uint64_t h = 14695981039346656037ull;
		size_t l = 0;
		uint32_t k;

		if (p->fe[i].f == &p->builtin)
			continue;
		for (; s[l]; l++)
			h = (h ^ (unsigned char)s[l]) * 1099511628211ull;
		for (k = (uint32_t)(h ^ (h >> 32)) & (cap - 1); tab[k]; k = (k + 1) & (cap - 1))
			if (!strcmp(names[tab[k] - 1], s))
				break;
		if (!tab[k] && n < max) {
			names[n++] = s;
			tab[k] = (uint32_t)n;
		}
	}
	if (tab != stk)
		free(tab);
	return n;
}

void pp_close(pp *p)
{
	if (p->cx) munmap(p->cx, MAXCTX * sizeof(ctx));
	if (p->perm.base) munmap(p->perm.base, (size_t)(p->perm.end - p->perm.base));
	if (p->tmp.base) munmap(p->tmp.base, (size_t)(p->tmp.end - p->tmp.base));
	if (p->sv) munmap(p->sv, SVMAX * sizeof(ptok));
	if (p->fe) munmap(p->fe, (1u << 20) * sizeof(fent));
	if (p->lm) munmap(p->lm, 65536 * sizeof(*p->lm));
	if (p->out) {
		if (out_spare_cap < p->capout) {
			if (out_spare)
				munmap(out_spare, out_spare_cap);
			out_spare = p->out, out_spare_cap = p->capout;
		} else {
			munmap(p->out, p->capout);
		}
	}
	if (p->rec.tk) {
		if (!tk_spare) {
			tk_spare = p->rec.tk, sb_spare = p->rec.sb;
		} else {
			munmap(p->rec.tk, HC_MAXTK * sizeof(ptok));
			munmap(p->rec.sb, HC_MAXSB);
		}
	}
	if (p->rec.t && !rec_spare.t) {
		rec_spare.t = p->rec.t, rec_spare.ord = p->rec.ord, rec_spare.ctr = p->rec.ctr;
		rec_spare.crd = p->rec.crd, rec_spare.seg = p->rec.seg, rec_spare.cp = p->rec.cp;
	} else if (p->rec.t) {
		munmap(p->rec.t, HC_MAXT * sizeof(htouch));
		munmap(p->rec.ord, HC_MAXORD * sizeof(honce));
		munmap(p->rec.ctr, HC_MAXCP * sizeof(hcpos));
		munmap(p->rec.crd, HC_MAXCRD * sizeof(hcrd));
		munmap(p->rec.seg, HC_MAXSEG * sizeof(hsegr));
		munmap(p->rec.cp, (HC_MAXCP + 1) * sizeof(cprov));
	}
	munmap(p, sizeof(pp));
}
