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
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

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

/* ---------------- shared (process-wide) allocation ---------------- */

static char *shared_base;
static _Atomic size_t shared_used;
#define SHARED_MAX (1ull << 32)

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

typedef struct pfile {
	const pdir *dir;
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
enum { MF_VARIADIC = 1, MF_PASTE = 2, MF_VAOPT = 4 };
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

typedef struct pmacro {
	const vtok *body;
	const char *src;
	const uint8_t *pidx;	/* per body token: parameter index + 1, or 0 */
	uint32_t nbody, name;
	uint16_t nparam;
	uint8_t kind, flags, builtin;
} pmacro;

#define MPAGE_BITS 11
#define MPAGE (1u << MPAGE_BITS)
#define NMDIR ((1u << 20) >> MPAGE_BITS)

/* ---------------- contexts ---------------- */

enum { C_FILE, C_VTOK, C_PTOK };

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
} ctx;

typedef struct fent {
	uint32_t base;
	const vlex_file *f;
	const pfile *fi;
	const char *name;	/* for __FILE__ (prefix map applied) */
	uint32_t namelen;
} fent;

#define MAXCTX (1u << 15)
#define SVMAX (1u << 24)

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
	struct lmark {		/* #line / GNU line markers */
		uint32_t fe, off;	/* in fent fe, from byte offset off on */
		uint32_t pline, line;	/* physical line pline is line */
		const char *name;	/* presumed file name, or NULL */
		uint32_t namelen;
	} *lm;
	uint32_t nlm;
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
	va_list ap;

	if (e)
		fprintf(stderr, "%s:%u: %s: ", e->f->path, loc_line(p, loc), what);
	else
		fprintf(stderr, "vcc: %s: ", what);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	if (what[0] == 'e')
		p->errors++;
	if (what[0] == 'f')
		exit(1);
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

/* ---------------- tokens ---------------- */

static inline void vfill(ptok *t, const vtok *v, const char *src, uint32_t loc)
{
	t->s = src + v->off;
	t->len = v->len;
	t->id = v->id;
	t->loc = loc;
	t->kind = v->kind;
	t->flags = v->flags & (TF_SOL | TF_SPACE | TF_DIRTY | TF_RAW);
	t->_pad = 0;
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
		if (c->barrier) {
			mkeof(t);
			return;
		}
		if (c->type == C_FILE) {
			if (p->nfile == 1) {
				mkeof(t);
				return;
			}
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
			return (c->type == C_PTOK ? c->pt[c->i].kind : c->vt[c->i].kind) == T_LPAREN;
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
		e = mget(p, t->id);
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

static bool collect(pp *p, const pmacro *m, const ptok *name, margs *a, ptok *rp)
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

/* clang's MacroArgs::ArgNeedsPreexpansion: any identifier with a macro */
static bool needs_pre(const pp *p, const ptok *t, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (t[i].kind == T_IDENT && mget(p, t[i].id))
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
	b = w = alloc(&p->tmp, cap + 16);
	*w++ = '"';
	for (uint32_t i = 0; i < n; i++) {
		if (i && (t[i].flags & (TF_SPACE | TF_SOL)))
			*w++ = ' ';
		if (is_strlit(t[i].kind)) {
			char tmp[t[i].len + 1];
			size_t l;
			const char *s = pp_spell(&t[i], tmp, &l);

			for (size_t j = 0; j < l; j++) {
				if (s[j] == '\\' || s[j] == '"')
					*w++ = '\\';
				if (s[j] == '\n') {
					*w++ = '\\';
					*w++ = 'n';
					continue;
				}
				*w++ = s[j];
			}
		} else {
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

static ptok paste(pp *p, const ptok *l, const ptok *r)
{
	char *b = alloc(&p->tmp, l->len + r->len + 24), *w;
	ptok t = *l;
	int k;

	w = spell_to(b, l);
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
				cur = paste(p, &cur, &p->sv[i + 1]);
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
static bool trivial(const pp *p, const pmacro *m)
{
	const vtok *v = &m->body[0];
	uintptr_t e;

	if (v->kind != T_IDENT)
		return true;
	e = mget(p, v->id);
	if (e && !(e & 1) && v->id != m->name)
		return false;
	if (m->kind == MK_OBJ)
		return true;
	return !m->pidx || !m->pidx[0];
}

static void single(const pp *p, const pmacro *m, ptok *t, uint8_t nf, uint32_t eloc)
{
	uintptr_t e;

	vfill(t, &m->body[0], m->src, eloc);
	t->flags = (t->flags & ~(TF_SOL | TF_SPACE)) | nf;
	if (t->kind == T_IDENT && (e = mget(p, t->id)) && ((e & 1) || (const pmacro *)(e & ~(uintptr_t)1) == m))
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
	int n = snprintf(b, 24, "%llu", v);

	memset(b + n, 0, 16);
	t->s = b;
	t->len = (uint32_t)n;
	t->kind = T_NUMBER;
	t->id = 0;
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
				     bool angled, bool next, int32_t *sdir, const char **path);
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
		mkstr(p, t, p->base_name, p->base_namelen);
		break;
	case B_COUNTER:
		mknum(p, t, p->counter++);
		break;
	case B_INCLUDE_LEVEL:
		mknum(p, t, p->nfile > 1 ? (unsigned)p->nfile - 1 : 0);
		break;
	case B_DATE: case B_TIME: case B_TIMESTAMP: {
		time_t now = time(NULL);
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
		mknum(p, t, find_include(p, fc, buf, n, angled, m->builtin == B_HAS_INCLUDE_NEXT, &sd, &path) != NULL);
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
			r.v = mget(p, n.id) != 0;
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
	eadv(&E);
	r = ecomma(&E, true);
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
		def = mget(p, c->vt[b].id) != 0;
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

static void define(pp *p, ctx *c, uint32_t b, uint32_t e)
{
	const vtok *vt = c->vt;
	pmacro *m;
	uint32_t params[256], np = 0, i = b + 1;
	uint8_t *pidx = NULL;
	bool va = false;

	if (b >= e || vt[b].kind != T_IDENT) {
		error(p, c->loc + vt[b - 1].off, "macro name must be an identifier");
		return;
	}
	if (vt[b].id == KW_defined) {
		error(p, c->loc + vt[b].off, "'defined' cannot be used as a macro name");
		return;
	}
	m = alloc(&p->perm, sizeof(*m));
	memset(m, 0, sizeof(*m));
	m->name = vt[b].id;
	m->src = c->src;
	m->kind = MK_OBJ;
	if (i < e && vt[i].kind == T_LPAREN && !(vt[i].flags & TF_SPACE)) {
		m->kind = MK_FUNC;
		i++;
		if (i < e && vt[i].kind == T_RPAREN) {
			i++;
		} else {
			for (;;) {
				if (i >= e)
					goto bad;
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
					goto bad;
				}
				if (i < e && vt[i].kind == T_RPAREN && (i++, 1))
					break;
				if (va || i >= e || vt[i].kind != T_COMMA)
					goto bad;
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
	if (np) {
		pidx = alloc(&p->perm, m->nbody + 1);
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
	if (m->nbody && (m->body[0].kind == T_HASHHASH || m->body[m->nbody - 1].kind == T_HASHHASH)) {
		error(p, c->loc + vt[b].off, "'##' cannot appear at either end of a macro expansion");
		return;
	}
	if (m->kind == MK_FUNC)
		for (uint32_t j = 0; j < m->nbody; j++)
			if (m->body[j].kind == T_HASH && !(j + 1 < m->nbody && pidx && pidx[j + 1]) &&
			    !(j + 1 < m->nbody && m->body[j + 1].kind == T_IDENT && m->body[j + 1].id == KW_VA_OPT)) {
				error(p, c->loc + m->body[j].off, "'#' is not followed by a macro parameter");
				return;
			}
	mset(p, m->name, m);
	return;
bad:
	error(p, c->loc + vt[b].off, "invalid macro parameter list");
}

static char *pstrdup(pp *p, const char *s, size_t n)
{
	char *d = alloc(&p->perm, n + 1);

	memcpy(d, s, n);
	d[n] = 0;
	return d;
}

static const vlex_file *find_include(pp *p, const ctx *from, const char *name, size_t nlen,
				     bool angled, bool next, int32_t *sdir, const char **path)
{
	char buf[4096];
	const vlex_file *f;
	int32_t start;

	*sdir = -1;
	if (nlen + 2 >= sizeof(buf))
		return NULL;
	if (name[0] == '/') {
		memcpy(buf, name, nlen);
		buf[nlen] = 0;
		*path = pstrdup(p, buf, nlen);
		return vlex_get(buf, nlen);
	}
	if (next && from && from->sdir < 0)
		next = false;	/* #include_next outside the search path: a normal search */
	if (!angled && !next) {
		const char *fp = from && from->f != &p->builtin ? from->f->path : "";
		const char *sl = strrchr(fp, '/');
		size_t dl = sl ? (size_t)(sl - fp) + 1 : 0;

		if (dl + nlen + 1 < sizeof(buf)) {
			memcpy(buf, fp, dl);
			memcpy(buf + dl, name, nlen);
			buf[dl + nlen] = 0;
			if ((f = vlex_get(buf, dl + nlen))) {
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
		if ((f = vlex_get(buf, l))) {
			*sdir = k;
			*path = pstrdup(p, buf, l);
			return f;
		}
	}
	/* frameworks: <Fw/h.h> -> DIR/Fw.framework/Headers/h.h */
	if (angled && !next) {
		const char *sl = memchr(name, '/', nlen);

		for (int k = 0; sl && k < p->o->nfwdirs; k++) {
			int l = snprintf(buf, sizeof(buf), "%s/%.*s.framework/Headers/%.*s", p->o->fwdirs[k],
					 (int)(sl - name), name, (int)(nlen - (size_t)(sl - name) - 1), sl + 1);

			if (l > 0 && (size_t)l < sizeof(buf) && (f = vlex_get(buf, (size_t)l))) {
				*path = pstrdup(p, buf, (size_t)l);
				return f;
			}
		}
	}
	return NULL;
}

static void add_fent(pp *p, ctx *c, const vlex_file *f, const char *name)
{
	size_t nl = strlen(name);
	const char *from = p->o->prefix_from;
	fent *e = &p->fe[p->nfe];

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

static void enter_file(pp *p, const vlex_file *f, const char *path, int32_t sdir)
{
	const pfile *fi = pfile_get(f);
	ctx *c;

	if (fi->guard && mget(p, fi->guard))
		return;
	for (uint32_t i = 0; i < p->nonce; i++)
		if (p->once[i] == f)
			return;
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
	p->nfile++;
	add_fent(p, c, f, path);
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

	push_line(p, c, b, e);
	raw(p, &t);
	if (t.kind == T_STRING || t.kind == T_LESS || t.kind == T_IDENT) {
		if (!header_name(p, buf, sizeof(buf), &n, &angled, &t)) {
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
	f = find_include(p, c, buf, n, angled, kind == D_INCLUDE_NEXT, &sd, &path);
	if (!f)
		fatal(p, loc, "'%.*s' file not found", (int)n, buf);
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

		if (id && p->npushed < 4096) {
			p->pushed[p->npushed].id = id;
			p->pushed[p->npushed++].m = mget(p, id) & ~(uintptr_t)1;
		}
		return 0;
	}
	if (word(&q, e, "pop_macro")) {
		uint32_t id = pragma_macro_name(q, (size_t)(e - q));

		for (uint32_t i = p->npushed; i-- > 0;)
			if (p->pushed[i].id == id) {
				mset(p, id, (const pmacro *)p->pushed[i].m);
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
		next_exp(p, &n);
		next_exp(p, &f);
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
			mset(p, c->vt[b + 1].id, NULL);
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
	case D_LINE: case D_LINEMARK:
		line_directive(p, c, d->kind == D_LINE ? b + 1 : b, e, d->kind == D_LINE, loc);
		return 0;
	default:
		error(p, loc, "invalid preprocessing directive");
		return 0;
	}
}

/* ---------------- setup ---------------- */

static void def_builtin(pp *p, const char *name, int b)
{
	pmacro *m = alloc(&p->perm, sizeof(*m));

	memset(m, 0, sizeof(*m));
	m->kind = MK_BUILTIN;
	m->builtin = (uint8_t)b;
	m->name = vintern_copy(name, strlen(name));
	mset(p, m->name, m);
}

pp *pp_open(const pp_opts *o)
{
	pp *p = vm(sizeof(pp));
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
	p->sv = vm(SVMAX * sizeof(ptok));
	p->fe = vm((1u << 20) * sizeof(fent));
	p->once = alloc(&p->perm, 4096 * sizeof(*p->once));
	p->pushed = alloc(&p->perm, 4096 * sizeof(*p->pushed));
	p->lm = vm(65536 * sizeof(*p->lm));
	for (size_t i = 0; i < sizeof(bi) / sizeof(bi[0]); i++)
		def_builtin(p, bi[i].n, bi[i].b);

	mf = vlex_get(o->main, strlen(o->main));
	if (!mf) {
		fprintf(stderr, "vcc: %s: cannot read\n", o->main);
		exit(1);
	}
	enter_file(p, mf, o->main, -1);
	p->base_name = p->fe[0].name;
	p->base_namelen = p->fe[0].namelen;

	/* "<built-in>": the predefines, -D/-U and -include, on top of the main file */
	src = alloc(&p->perm, n + 129);
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

void pp_next(pp *p, ptok *t)
{
	if (p->depth == p->nfile && !p->svn)
		p->tmp.p = p->tmp.base;	/* nothing above file level refers to tmp */
	next_exp(p, t);
}

int pp_errors(const pp *p)
{
	return p->errors;
}
