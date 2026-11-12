/*
 * The text form, parsing (SPEC-vir.md 11.3-11.5).  Safe on arbitrary bytes:
 * every error is reported as file:line:col by the caller and never crashes.
 *
 * The whole text is lexed first.  A function is read into parser-local
 * records (instructions in text order, operands as `pop`s); at its closing
 * brace the names are resolved, the constants created with the type their
 * position requires (11.5), and the function assembled in the constants-
 * first layout of 2.3.  Nothing is repaired: what the text says is what the
 * verifier sees (a block without a terminator is S3, not a syntax error).
 */
#include <errno.h>
#include <setjmp.h>

#include "virimpl.h"

/* ---------------- growable vectors and name maps ---------------- */

#define PV(T) struct { T *v; uint32_t n, cap; }
#define PV_PUSH(a, x) do { \
	if ((a).n == (a).cap) { \
		(a).cap = (a).cap ? (a).cap * 2 : 64; \
		(a).v = realloc((a).v, (size_t)(a).cap * sizeof(*(a).v)); \
		if (!(a).v) abort(); \
	} \
	(a).v[(a).n++] = (x); \
} while (0)
#define PV_FREE(a) do { free((a).v); (a).v = NULL; (a).n = (a).cap = 0; } while (0)

typedef struct nent {
	const char *s;
	uint32_t n, h;
	uint32_t st;		/* 0 not defined, 1 defined */
	uint32_t id;		/* what it names */
	uint32_t line, col;	/* first mention */
} nent;
typedef struct nm {
	uint32_t *tab, cap;
	PV(nent) e;
} nm;

static uint32_t nm_hash(const char *s, uint32_t n)
{
	uint32_t h = 2166136261u;

	for (uint32_t i = 0; i < n; i++)
		h = (h ^ (unsigned char)s[i]) * 16777619u;
	return h;
}

/* the entry for a name; *isnew when it was created (id and st 0) */
static uint32_t nm_get(nm *m, const char *s, uint32_t n, bool *isnew)
{
	uint32_t h = nm_hash(s, n);

	if (m->e.n * 2 >= m->cap) {
		uint32_t nc = m->cap ? m->cap * 2 : 64;

		free(m->tab);
		m->tab = calloc(nc, 4);
		if (!m->tab)
			abort();
		m->cap = nc;
		for (uint32_t i = 0; i < m->e.n; i++) {
			uint32_t j = m->e.v[i].h & (nc - 1);

			while (m->tab[j])
				j = (j + 1) & (nc - 1);
			m->tab[j] = i + 1;
		}
	}
	for (uint32_t j = h & (m->cap - 1); m->tab[j]; j = (j + 1) & (m->cap - 1)) {
		nent *e = &m->e.v[m->tab[j] - 1];

		if (e->h == h && e->n == n && !memcmp(e->s, s, n)) {
			*isnew = false;
			return m->tab[j] - 1;
		}
	}
	{
		nent e = { s, n, h, 0, 0, 0, 0 };
		uint32_t j = h & (m->cap - 1);

		while (m->tab[j])
			j = (j + 1) & (m->cap - 1);
		PV_PUSH(m->e, e);
		m->tab[j] = m->e.n;
		*isnew = true;
		return m->e.n - 1;
	}
}

static void nm_reset(nm *m)
{
	m->e.n = 0;
	if (m->tab)
		memset(m->tab, 0, (size_t)m->cap * 4);
}

static void nm_free(nm *m)
{
	free(m->tab);
	PV_FREE(m->e);
	m->tab = NULL;
	m->cap = 0;
}

/* ---------------- tokens ---------------- */

enum { T_EOF, T_NL, T_ID, T_INT, T_FLT, T_STR, T_SYM, T_SLOT, T_ASM, T_SIG, T_PUNCT, T_ARROW, T_DOTS, T_DOTDOT };

typedef struct tok {
	uint8_t k, ch;
	uint32_t line, col, len;
	const char *s;
	uint64_t v;		/* INT: the value; STR: offset in the string buffer */
} tok;

/* a pop: an operand as written; resolved when the function is complete */
enum { PK_NAME, PK_INT, PK_FLT, PK_BITS, PK_SYM, PK_SLOT, PK_LABEL };
typedef struct pop {
	uint8_t kind, ty, neg;	/* ty: the suffix, or 0xff */
	uint32_t line, col;
	uint32_t id;		/* name entry, unit symbol, slot, block name entry */
	uint64_t v;		/* integer (two's complement), float bits (as a double or the bits) */
	int64_t add;		/* addend of a symbol or slot */
} pop;

enum { FX_VAL, FX_BLK };
enum { WM_POS, WM_LIST, WM_BR };	/* how the wanted type of a value is known */
typedef struct fixup {
	uint8_t kind, mode, fld;	/* fld: 0..2 an instruction field, 3 an extra word */
	uint32_t k;		/* the instruction (index into B) */
	uint32_t at;		/* the extra word */
	uint32_t pop;		/* FX_VAL: the pop; FX_BLK: the block name entry */
	uint32_t aux;		/* WM_BR: the target's name entry (the argument index is `at - the list start` via cnt) */
	uint32_t idx;		/* WM_BR: the argument index */
	uint32_t line, col;
} fixup;

typedef struct pcase { uint64_t lo, hi; uint32_t bn, line, col; } pcase;
typedef struct ppos { uint32_t line, col, cnt, c0, c1; } ppos;	/* per instruction; cnt: br args, switch cases [c0, c1) */
typedef struct pblk { uint32_t first, n, nparam, fl, name; } pblk;
typedef struct cdef { vir_ins in; uint32_t pop, name; } cdef;	/* a constant written as an instruction line */

typedef struct ps {
	const char *src;
	vir_unit *u;
	vir_perr *err;
	jmp_buf jb;
	PV(tok) t;
	char *sbuf;		/* decoded strings */
	size_t snb, scap;
	uint32_t pos;		/* the token index */
	/* unit level */
	nm signame, asmname;
	/* per function */
	vir_fn f;
	bool infn;
	nm vals, blks, slots;
	PV(vir_ins) B;
	PV(ppos) bp;
	PV(pblk) pb;
	PV(pop) pops;
	PV(fixup) fix;
	PV(pcase) cases;
	PV(cdef) cdefs;
	PV(uint32_t) sigoff;	/* per unit signature: its offset in the function's extra, or ~0 */
	PV(uint32_t) scratch;
	uint32_t fsym;
	uint32_t fsigoff;
} ps;

static void perr_at(ps *p, uint32_t line, uint32_t col, const char *fmt, ...) __attribute__((noreturn, format(printf, 4, 5)));
static void perr_at(ps *p, uint32_t line, uint32_t col, const char *fmt, ...)
{
	va_list ap;

	p->err->line = line;
	p->err->col = col;
	va_start(ap, fmt);
	vsnprintf(p->err->msg, sizeof(p->err->msg), fmt, ap);
	va_end(ap);
	longjmp(p->jb, 1);
}

#define PERR(p, t, ...) perr_at((p), (t)->line, (t)->col, __VA_ARGS__)

/* ---------------- lexer ---------------- */

static inline bool idstart(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static inline bool idchar(int c) { return idstart(c) || (c >= '0' && c <= '9') || c == '.'; }
static inline bool symchar(int c) { return idchar(c) || c == '$'; }
static inline int hexval(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static void sput(ps *p, int c)
{
	if (p->snb == p->scap) {
		p->scap = p->scap ? p->scap * 2 : 256;
		p->sbuf = realloc(p->sbuf, p->scap);
		if (!p->sbuf)
			abort();
	}
	p->sbuf[p->snb++] = (char)c;
}

static void lex_all(ps *p, const char *src, size_t n)
{
	const char *c = src, *end = src + n, *ls = src;
	uint32_t line = 1;
	bool nl = true;

	while (c < end) {
		tok t = { 0 };

		if (*c == ' ' || *c == '\t' || *c == '\r') {
			c++;
			continue;
		}
		if (*c == ';') {
			while (c < end && *c != '\n')
				c++;
			continue;
		}
		t.line = line;
		t.col = (uint32_t)(c - ls) + 1;
		t.s = c;
		if (*c == '\n') {
			c++;
			line++;
			ls = c;
			if (!nl) {
				t.k = T_NL;
				t.len = 1;
				PV_PUSH(p->t, t);
				nl = true;
			}
			continue;
		}
		nl = false;
		if (idstart(*c)) {
			while (c < end && idchar(*c))
				c++;
			t.k = T_ID;
		} else if (*c >= '0' && *c <= '9') {
			uint64_t v = 0;
			bool ovf = false, flt = false;

			if (*c == '0' && c + 1 < end && (c[1] == 'x' || c[1] == 'X') && c + 2 < end && hexval(c[2]) >= 0) {
				const char *q = c + 2;

				while (q < end && hexval(*q) >= 0)
					q++;
				if (q < end && *q == '.' && q + 1 < end) {	/* a hexadecimal float needs its p exponent */
					const char *r = q + 1;

					while (r < end && hexval(*r) >= 0)
						r++;
					if (r < end && (*r == 'p' || *r == 'P'))
						flt = true, q = r;
				}
				if (!flt && q < end && (*q == 'p' || *q == 'P'))
					flt = true;
				if (flt) {	/* p, an optional sign, digits */
					q++;
					if (q < end && (*q == '+' || *q == '-'))
						q++;
					if (q >= end || *q < '0' || *q > '9') {
						p->err->line = t.line, p->err->col = t.col;
						snprintf(p->err->msg, sizeof(p->err->msg), "malformed hexadecimal float");
						longjmp(p->jb, 1);
					}
					while (q < end && *q >= '0' && *q <= '9')
						q++;
					c = q;
					t.k = T_FLT;
				} else {
					for (c += 2; c < end && hexval(*c) >= 0; c++) {
						if (v >> 60)
							ovf = true;
						v = v << 4 | (uint64_t)hexval(*c);
					}
					t.k = T_INT;
				}
			} else {
				for (; c < end && *c >= '0' && *c <= '9'; c++) {
					unsigned d = (unsigned)(*c - '0');

					if (v > (UINT64_MAX - d) / 10)
						ovf = true;
					v = v * 10 + d;
				}
				t.k = T_INT;
			}
			if (ovf || (c < end && (idstart(*c) || (*c >= '0' && *c <= '9')))) {
				p->err->line = t.line, p->err->col = t.col;
				snprintf(p->err->msg, sizeof(p->err->msg), ovf ? "integer constant too large" : "malformed number");
				longjmp(p->jb, 1);
			}
			t.v = v;
		} else if (*c == '"') {
			t.k = T_STR;
			t.v = p->snb;
			c++;
			for (;;) {
				int ch;

				if (c >= end || *c == '\n') {
					p->err->line = t.line, p->err->col = t.col;
					snprintf(p->err->msg, sizeof(p->err->msg), "unterminated string");
					longjmp(p->jb, 1);
				}
				if (*c == '"') {
					c++;
					break;
				}
				if (*c != '\\') {
					sput(p, (unsigned char)*c++);
					continue;
				}
				c++;
				if (c >= end)
					continue;
				ch = *c++;
				switch (ch) {
				case 'n': sput(p, '\n'); break;
				case 't': sput(p, '\t'); break;
				case 'r': sput(p, '\r'); break;
				case 'a': sput(p, 7); break;
				case 'b': sput(p, 8); break;
				case 'f': sput(p, 12); break;
				case 'v': sput(p, 11); break;
				case '\\': case '"': case '\'': case '?': sput(p, ch); break;
				case 'x': {
					int v = 0, nd = 0;

					while (c < end && hexval(*c) >= 0) {
						v = (v << 4 | hexval(*c)) & 255;
						c++;
						nd++;
					}
					if (!nd) {
						p->err->line = line, p->err->col = (uint32_t)(c - ls) + 1;
						snprintf(p->err->msg, sizeof(p->err->msg), "\\x without digits");
						longjmp(p->jb, 1);
					}
					sput(p, v);
					break;
				}
				default:
					if (ch >= '0' && ch <= '7') {
						int v = ch - '0';

						for (int i = 0; i < 2 && c < end && *c >= '0' && *c <= '7'; i++)
							v = v * 8 + (*c++ - '0');
						sput(p, v & 255);
					} else {
						p->err->line = line, p->err->col = (uint32_t)(c - ls);
						snprintf(p->err->msg, sizeof(p->err->msg), "unknown escape sequence");
						longjmp(p->jb, 1);
					}
				}
			}
			t.len = (uint32_t)(p->snb - t.v);
			PV_PUSH(p->t, t);
			continue;
		} else if (*c == '@' || *c == '$' || *c == '!' || *c == '%') {
			t.k = *c == '@' ? T_SYM : *c == '$' ? T_SLOT : *c == '!' ? T_ASM : T_SIG;
			c++;
			t.s = c;
			while (c < end && (t.k == T_SYM ? symchar(*c) : idchar(*c)))
				c++;
			if (c == t.s) {
				p->err->line = t.line, p->err->col = t.col;
				snprintf(p->err->msg, sizeof(p->err->msg), "a name is expected after `%c`", t.s[-1]);
				longjmp(p->jb, 1);
			}
			t.len = (uint32_t)(c - t.s);
			PV_PUSH(p->t, t);
			continue;
		} else if (c + 2 < end + 0 && c[0] == '.' && c[1] == '.' && c[2] == '.') {
			t.k = T_DOTS;
			c += 3;
		} else if (c + 1 < end && c[0] == '.' && c[1] == '.') {
			t.k = T_DOTDOT;
			c += 2;
		} else if (c + 1 < end && c[0] == '-' && c[1] == '>') {
			t.k = T_ARROW;
			c += 2;
		} else if (strchr(",:=(){}[]&+-.", *c) && *c) {
			t.k = T_PUNCT;
			t.ch = (uint8_t)*c++;
		} else {
			p->err->line = t.line;
			p->err->col = t.col;
			snprintf(p->err->msg, sizeof(p->err->msg), "unexpected character 0x%02x", (unsigned char)*c);
			longjmp(p->jb, 1);
		}
		t.len = (uint32_t)(c - t.s);
		PV_PUSH(p->t, t);
	}
	{
		tok t = { 0 };

		if (!nl) {
			t.k = T_NL;
			t.line = line;
			t.col = (uint32_t)(c - ls) + 1;
			PV_PUSH(p->t, t);
		}
		t.k = T_EOF;
		t.line = line;
		t.col = (uint32_t)(c - ls) + 1;
		PV_PUSH(p->t, t);
	}
}

/* ---------------- token helpers ---------------- */

static tok *cur(ps *p) { return &p->t.v[p->pos]; }
static tok *peek(ps *p, uint32_t k) { return &p->t.v[p->pos + k < p->t.n ? p->pos + k : p->t.n - 1]; }
static tok *next(ps *p)
{
	tok *t = cur(p);

	if (t->k != T_EOF)
		p->pos++;
	return t;
}
static bool is_p(const tok *t, char ch) { return t->k == T_PUNCT && t->ch == ch; }
static bool is_id(const tok *t, const char *s) { return t->k == T_ID && strlen(s) == t->len && !memcmp(t->s, s, t->len); }
static bool accept_p(ps *p, char ch)
{
	if (is_p(cur(p), ch)) {
		p->pos++;
		return true;
	}
	return false;
}
static void expect_p(ps *p, char ch)
{
	if (!accept_p(p, ch))
		PERR(p, cur(p), "expected `%c`", ch);
}
static tok *expect_k(ps *p, int k, const char *what)
{
	if (cur(p)->k != k)
		PERR(p, cur(p), "expected %s", what);
	return next(p);
}
static void expect_kw(ps *p, const char *kw)
{
	if (!is_id(cur(p), kw))
		PERR(p, cur(p), "expected `%s`", kw);
	p->pos++;
}
static void expect_nl(ps *p)
{
	if (cur(p)->k != T_NL && cur(p)->k != T_EOF)
		PERR(p, cur(p), "unexpected text at the end of the line");
	if (cur(p)->k == T_NL)
		p->pos++;
}

static uint64_t expect_int(ps *p, bool allow_neg, bool *neg)
{
	bool ng = false;
	tok *t;

	if (allow_neg && accept_p(p, '-'))
		ng = true;
	t = expect_k(p, T_INT, "an integer");
	if (neg)
		*neg = ng;
	if (ng && t->v > (uint64_t)INT64_MAX + 1)
		PERR(p, t, "integer constant too large");
	return ng ? -t->v : t->v;
}

static uint32_t expect_u32(ps *p, const char *what, uint32_t max)
{
	tok *t = cur(p);
	uint64_t v = expect_int(p, false, NULL);

	if (v > max)
		PERR(p, t, "%s out of range", what);
	return (uint32_t)v;
}

static uint32_t type_of(const tok *t)
{
	for (uint32_t i = 1; i < VT_N; i++)
		if (t->k == T_ID && strlen(vir_tyname[i]) == t->len && !memcmp(vir_tyname[i], t->s, t->len))
			return i;
	return 0;
}

static uint32_t expect_type(ps *p)
{
	tok *t = cur(p);
	uint32_t ty = type_of(t);

	if (!ty)
		PERR(p, t, "expected a type (i32, i64, f32, f64, ptr, multi)");
	p->pos++;
	return ty;
}

static const char *str_of(ps *p, const tok *t, size_t *n)
{
	*n = t->len;
	return vir_unit_str(p->u, p->sbuf ? p->sbuf + t->v : "", t->len);
}

static uint32_t log2_align(ps *p, const tok *t, uint64_t a, uint32_t maxlog)
{
	uint32_t l = 0;

	if (!a || (a & (a - 1)))
		PERR(p, t, "an alignment must be a power of two");
	while (((uint64_t)1 << l) < a)
		l++;
	if (l > maxlog)
		PERR(p, t, "alignment too large");
	return l;
}

/* ---------------- signatures, groups ---------------- */

static uint32_t parse_group(ps *p, uint32_t *w1)
{
	tok *t = cur(p);

	*w1 = 0;
	if (is_id(t, "none")) {
		p->pos++;
		return VG_WORD0(VG_NONE, 0, 0, 0, 0, 0, 0);
	}
	if (is_id(t, "sret")) {
		p->pos++;
		return VG_WORD0(VG_SRET, 0, 0, 0, 0, 0, 0);
	}
	if (is_id(t, "mem")) {
		uint64_t sz, al;
		tok *at;

		p->pos++;
		at = cur(p);
		sz = expect_int(p, false, NULL);
		if (sz >= ((uint64_t)1 << 28))
			PERR(p, at, "a memory group's size must be below 2^28");
		expect_kw(p, "align");
		at = cur(p);
		al = expect_int(p, false, NULL);
		*w1 = VG_WORD1(sz, log2_align(p, at, al, 15));
		return VG_WORD0(VG_MEM, 0, 0, 0, 0, 0, 0);
	}
	{
		uint32_t cl[4] = { 0, 0, 0, 0 }, n = 0;

		for (;;) {
			tok *pt = cur(p);
			uint32_t c = is_id(pt, "int") ? VC_INT : is_id(pt, "f32") ? VC_F32 : is_id(pt, "f64") ? VC_F64 : 0;

			if (!c)
				PERR(p, pt, "expected a group (none, int, f32, f64, mem, sret)");
			if (n == 4)
				PERR(p, pt, "at most 4 pieces in a group");
			cl[n++] = c;
			p->pos++;
			if (!accept_p(p, ':'))
				break;
		}
		return VG_WORD0(VG_REG, n, cl[0], cl[1], cl[2], cl[3], 0);
	}
}

static void parse_sigdecl(ps *p)
{
	tok *nt;
	bool isnew;
	uint32_t e, ng = 0, var = 0, w1, res0, res1;
	PV(uint32_t) w = { 0 };

	next(p);
	nt = expect_k(p, T_SIG, "a signature name (%name)");
	e = nm_get(&p->signame, nt->s, nt->len, &isnew);
	if (!isnew)
		PERR(p, nt, "signature %%%.*s declared twice", (int)nt->len, nt->s);
	expect_p(p, '=');
	expect_p(p, '(');
	if (!is_p(cur(p), ')')) {
		for (;;) {
			uint32_t w0;

			if (cur(p)->k == T_DOTS) {
				p->pos++;
				var = 1;
				break;
			}
			w0 = parse_group(p, &w1);
			if (ng == 0xffff)
				PERR(p, cur(p), "too many groups");
			PV_PUSH(w, w0);
			PV_PUSH(w, w1);
			ng++;
			if (!accept_p(p, ','))
				break;
		}
	}
	expect_p(p, ')');
	expect_k(p, T_ARROW, "`->`");
	res0 = parse_group(p, &res1);
	expect_nl(p);
	{
		uint32_t *all = malloc((size_t)(3 + w.n) * 4);

		if (!all)
			abort();
		all[0] = VSIG_HDR(ng, var);
		all[1] = res0;
		all[2] = res1;
		if (w.n)
			memcpy(&all[3], w.v, (size_t)w.n * 4);
		p->signame.e.v[e].st = 1;
		p->signame.e.v[e].id = vir_unit_sigw(p->u, all, 3 + w.n);
		free(all);
	}
	PV_FREE(w);
}

/* ---------------- unit level ---------------- */

static uint32_t sym_ref(ps *p, const tok *t)
{
	uint32_t i = vir_unit_sym(p->u, t->s, t->len);

	if (!p->u->sym[i].line) {
		p->u->sym[i].line = t->line;
		p->u->sym[i].col = t->col;
	}
	return i;
}

static void parse_asmdecl(ps *p)
{
	tok *nt, *st;
	bool isnew;
	uint32_t e;
	vir_uasm a;
	const char *kw[3] = { "outs", "ins", "clobber" };
	uint32_t *cnt[3] = { &a.nout, &a.nin, &a.nclob };
	const char ***lst[3] = { &a.outs, &a.ins, &a.clob };

	memset(&a, 0, sizeof(a));
	next(p);
	nt = expect_k(p, T_ASM, "an asm template name (!name)");
	e = nm_get(&p->asmname, nt->s, nt->len, &isnew);
	if (!isnew)
		PERR(p, nt, "asm template !%.*s declared twice", (int)nt->len, nt->s);
	st = expect_k(p, T_STR, "the template string");
	{
		size_t n;

		a.tmpl = str_of(p, st, &n);
	}
	for (int k = 0; k < 3; k++) {
		PV(const char *) l = { 0 };

		expect_kw(p, kw[k]);
		expect_p(p, '(');
		if (!is_p(cur(p), ')'))
			for (;;) {
				tok *s = expect_k(p, T_STR, "a string");
				size_t n;
				const char *c = str_of(p, s, &n);

				PV_PUSH(l, c);
				if (!accept_p(p, ','))
					break;
			}
		expect_p(p, ')');
		*cnt[k] = l.n;
		*lst[k] = vir_unit_alloc(p->u, (size_t)l.n * sizeof(char *));
		if (l.n)
			memcpy((void *)*lst[k], l.v, (size_t)l.n * sizeof(char *));
		PV_FREE(l);
	}
	if (is_id(cur(p), "labels")) {
		p->pos++;
		a.labels = expect_u32(p, "the label count", 65535);
	}
	expect_nl(p);
	if (p->u->nas == p->u->cas) {
		p->u->cas = p->u->cas ? p->u->cas * 2 : 8;
		p->u->as = realloc(p->u->as, (size_t)p->u->cas * sizeof(vir_uasm));
		if (!p->u->as)
			abort();
	}
	p->u->as[p->u->nas] = a;
	p->asmname.e.v[e].st = 1;
	p->asmname.e.v[e].id = p->u->nas++;
}

static void define_sym(ps *p, tok *t, uint32_t kind)
{
	uint32_t s = sym_ref(p, t);
	vir_usym *y = &p->u->sym[s];

	if (y->kind != VU_UNDECL)
		PERR(p, t, "symbol @%.*s defined twice", (int)t->len, t->s);
	y->kind = (uint8_t)kind;
}

static void parse_datadecl(ps *p)
{
	bool tls = is_id(cur(p), "tdata");
	tok *st, *at;
	uint32_t s, al;
	vir_unit *u = p->u;

	next(p);
	st = expect_k(p, T_SYM, "a symbol (@name)");
	define_sym(p, st, tls ? VU_TDATA : VU_DATA);
	s = sym_ref(p, st);
	expect_kw(p, "align");
	at = cur(p);
	al = expect_u32(p, "the alignment", 1u << 15);
	if (!al || (al & (al - 1)))
		PERR(p, at, "an alignment must be a power of two");
	u->sym[s].align = al;
	expect_p(p, '=');
	expect_p(p, '{');
	u->sym[s].item0 = u->nitem;
	if (!is_p(cur(p), '}'))
		for (;;) {
			vir_item it;
			tok *t = cur(p);

			memset(&it, 0, sizeof(it));
			if (is_id(t, "zero")) {
				p->pos++;
				it.ty = 0xff;
				it.v = expect_int(p, false, NULL);
			} else if (is_id(t, "bytes")) {
				tok *b;
				size_t n;

				p->pos++;
				b = expect_k(p, T_STR, "a string");
				it.ty = 0xfe;
				it.bytes = str_of(p, b, &n);
				it.v = n;
			} else {
				uint32_t ty = type_of(t);
				bool neg = false;

				if (!ty || ty == VT_VOID || ty == VT_MULTI)
					PERR(p, t, "expected a data item (i32, i64, f32, f64, ptr, zero, bytes)");
				p->pos++;
				it.ty = (uint8_t)ty;
				if (ty == VT_PTR && cur(p)->k == T_SYM) {
					tok *y = next(p);

					it.sym = sym_ref(p, y) + 1;
					if (is_p(cur(p), '+') || is_p(cur(p), '-')) {
						bool ng = is_p(cur(p), '-');

						p->pos++;
						it.v = expect_int(p, false, NULL);
						if (ng)
							it.v = -it.v;
					}
				} else if (ty == VT_F32 || ty == VT_F64) {
					bool ng = accept_p(p, '-');
					tok *f = cur(p);
					char tmp[96];

					if (f->k == T_ID && is_id(f, "bits") && is_p(peek(p, 1), ':')) {
						p->pos += 2;
						it.v = expect_int(p, false, NULL);
					} else {
						double d;

						if (f->k != T_FLT && f->k != T_INT)
							PERR(p, f, "expected a floating-point constant");
						if (f->len >= sizeof(tmp))
							PERR(p, f, "floating-point constant too long");
						memcpy(tmp, f->s, f->len);
						tmp[f->len] = 0;
						d = strtod(tmp, NULL);
						p->pos++;
						if (ng)
							d = -d;
						if (ty == VT_F32) {
							float fl = (float)d;

							if ((double)fl != d)
								PERR(p, f, "not exactly representable as f32");
							memcpy(&it.v, &fl, 4);
							it.v &= 0xffffffffu;
						} else {
							memcpy(&it.v, &d, 8);
						}
					}
				} else {
					it.v = expect_int(p, true, &neg);
					if (ty == VT_I32 && (neg ? (int64_t)it.v < INT32_MIN : it.v > UINT32_MAX))
						PERR(p, t, "constant out of range for i32");
				}
			}
			if (u->nitem == u->citem) {
				u->citem = u->citem ? u->citem * 2 : 64;
				u->item = realloc(u->item, (size_t)u->citem * sizeof(vir_item));
				if (!u->item)
					abort();
			}
			u->item[u->nitem++] = it;
			u->sym[s].nitem++;
			if (!accept_p(p, ','))
				break;
		}
	expect_p(p, '}');
	expect_nl(p);
}

/* ---------------- operands ---------------- */

static uint32_t push_pop(ps *p, pop o)
{
	PV_PUSH(p->pops, o);
	return p->pops.n - 1;
}

static void check_value_name(ps *p, const tok *t)
{
	if (t->len < 2 || t->s[0] != 'v')
		PERR(p, t, "expected a value (vN) or a constant");
}

/* the entry of a value or block name; blocks and values are separate tables */
static uint32_t name_ref(ps *p, nm *m, const tok *t)
{
	bool isnew;
	uint32_t e = nm_get(m, t->s, t->len, &isnew);

	if (isnew) {
		m->e.v[e].line = t->line;
		m->e.v[e].col = t->col;
	}
	return e;
}

static uint32_t block_ref(ps *p)
{
	tok *t = cur(p);

	if (t->k != T_ID || t->len < 2 || t->s[0] != 'b')
		PERR(p, t, "expected a block (bN)");
	p->pos++;
	return name_ref(p, &p->blks, t);
}

/* an operand as written: a value name or a constant with its optional :type */
static uint32_t parse_operand(ps *p)
{
	tok *t = cur(p);
	pop o;
	bool neg = false;

	memset(&o, 0, sizeof(o));
	o.ty = 0xff;
	o.line = t->line;
	o.col = t->col;
	if (t->k == T_ID && !(is_id(t, "bits") && is_p(peek(p, 1), ':') && peek(p, 2)->k == T_INT)) {
		check_value_name(p, t);
		p->pos++;
		o.kind = PK_NAME;
		o.id = name_ref(p, &p->vals, t);
		return push_pop(p, o);
	}
	if (t->k == T_ID) {	/* bits:0x... */
		p->pos += 2;
		o.kind = PK_BITS;
		o.v = expect_int(p, false, NULL);
	} else if (t->k == T_SYM || t->k == T_SLOT) {
		bool ng = false;

		p->pos++;
		o.add = 0;
		if (is_p(cur(p), '+') || is_p(cur(p), '-')) {
			ng = is_p(cur(p), '-');
			p->pos++;
			o.add = (int64_t)expect_int(p, false, NULL);
			if (ng)
				o.add = -o.add;
		}
		if (t->k == T_SYM) {
			o.kind = PK_SYM;
			o.id = sym_ref(p, t);
		} else {
			bool isnew;
			uint32_t e = nm_get(&p->slots, t->s, t->len, &isnew);

			if (isnew || !p->slots.e.v[e].st)
				PERR(p, t, "undefined slot $%.*s", (int)t->len, t->s);
			o.kind = PK_SLOT;
			o.id = p->slots.e.v[e].id;
		}
	} else if (is_p(t, '&')) {
		tok *b;

		p->pos++;
		b = cur(p);
		if (b->k != T_ID || b->len < 2 || b->s[0] != 'b')
			PERR(p, b, "expected a block (bN) after `&`");
		p->pos++;
		o.kind = PK_LABEL;
		o.id = name_ref(p, &p->blks, b);
	} else if (is_p(t, '-') || t->k == T_INT || t->k == T_FLT) {
		tok *n;

		if (accept_p(p, '-'))
			neg = true;
		n = cur(p);
		if (n->k == T_INT) {
			o.kind = PK_INT;
			o.v = expect_int(p, false, NULL);
			if (neg) {
				if (o.v > (uint64_t)INT64_MAX + 1)
					PERR(p, n, "integer constant too large");
				o.v = -o.v;
			}
		} else if (n->k == T_FLT) {
			char tmp[96];
			double d;

			if (n->len >= sizeof(tmp))
				PERR(p, n, "floating-point constant too long");
			memcpy(tmp, n->s, n->len);
			tmp[n->len] = 0;
			d = strtod(tmp, NULL);
			if (neg)
				d = -d;
			p->pos++;
			o.kind = PK_FLT;
			memcpy(&o.v, &d, 8);
		} else {
			PERR(p, n, "expected a constant");
		}
		o.neg = neg;
	} else {
		PERR(p, t, "expected a value or a constant");
	}
	if (is_p(cur(p), ':'))	/* the type suffix of a constant */
		if (type_of(peek(p, 1))) {
			p->pos++;
			o.ty = (uint8_t)expect_type(p);
		}
	return push_pop(p, o);
}

static void add_fix(ps *p, fixup x) { PV_PUSH(p->fix, x); }

/* an operand in field fld of the instruction about to be pushed */
static uint32_t fld_val(ps *p, uint32_t fld)
{
	uint32_t po = parse_operand(p);
	fixup x = { FX_VAL, WM_POS, (uint8_t)fld, p->B.n, 0, po, 0, 0, p->pops.v[po].line, p->pops.v[po].col };

	add_fix(p, x);
	return 0;
}

static uint32_t fld_blk(ps *p, uint32_t fld)
{
	tok *t = cur(p);
	uint32_t bn = block_ref(p);
	fixup x = { FX_BLK, 0, (uint8_t)fld, p->B.n, 0, bn, 0, 0, t->line, t->col };

	add_fix(p, x);
	return 0;
}

/* an extra word that holds a value or a block id: reserved by the caller, fixed up later */
static void list_val(ps *p, uint32_t at, uint32_t mode, uint32_t aux, uint32_t idx, uint32_t po)
{
	fixup x = { FX_VAL, (uint8_t)mode, 3, p->B.n, at, po, aux, idx, p->pops.v[po].line, p->pops.v[po].col };

	add_fix(p, x);
}

static void list_blk(ps *p, uint32_t at, uint32_t bn, tok *t)
{
	fixup x = { FX_BLK, 0, 3, p->B.n, at, bn, 0, 0, t->line, t->col };

	add_fix(p, x);
}

/* ---------------- flags ---------------- */

static bool low_bits_are_fields(uint32_t form)
{
	return form == FM_LOAD || form == FM_STORE || form == FM_ATOM || form == FM_EXT || form == FM_CC;
}

static void parse_flags(ps *p, vir_ins *in)
{
	const vir_opinfo *oi = &vir_optab[in->op];
	static const struct { const char *n; uint32_t bit; bool low; } F[] = {
		{ "nsw", VIF_NSW, 1 }, { "nuw", VIF_NUW, 1 }, { "exact", VIF_EXACT, 1 }, { "nz", VIF_NZ, 1 },
		{ "volatile", VIF_VOL, 0 }, { "noreturn", VIF_CALL_NORETURN, 1 }, { "pure", VIF_CALL_PURE, 1 },
		{ "const", VIF_CALL_CONST, 1 }, { "likely", VIF_LIKELY, 1 }, { "unlikely", VIF_UNLIKELY, 1 },
	};
	int nalign = 0;

	for (;;) {
		tok *t = cur(p);
		bool found = false;

		if (t->k != T_ID)
			return;
		if (is_id(t, "align")) {
			tok *at;
			uint32_t l;

			p->pos++;
			at = cur(p);
			l = log2_align(p, at, expect_int(p, false, NULL), 15);
			switch (oi->form) {
			case FM_LOAD: case FM_STORE: case FM_ATOM:
				in->fl = (uint16_t)((in->fl & ~0xf0) | l << 4);
				break;
			case FM_MEMCPY:
				if (in->op == VO_MEMCPY && nalign == 1) {
					in->fl = (uint16_t)((in->fl & ~0xf00) | l << 8);
					nalign = 2;
					continue;
				}
				if (nalign)
					PERR(p, t, "too many alignments");
				in->fl = (uint16_t)((in->fl & ~0xff0) | l << 4 | (in->op == VO_MEMCPY ? l << 8 : 0));
				nalign = 1;
				continue;
			default:
				PERR(p, t, "`align` is not allowed on %s", oi->name);
			}
			continue;
		}
		if (is_id(t, "tag")) {
			uint32_t v;

			p->pos++;
			v = expect_u32(p, "the tag", UINT32_MAX);
			if (in->op == VO_LOAD)
				in->b = v;
			else if (in->op == VO_STORE)
				in->c = v;
			else
				PERR(p, t, "`tag` is not allowed on %s", oi->name);
			continue;
		}
		for (size_t i = 0; i < sizeof(F) / sizeof(F[0]); i++)
			if (is_id(t, F[i].n)) {
				if (F[i].low && low_bits_are_fields(oi->form))
					PERR(p, t, "flag `%s` is not allowed on %s", F[i].n, oi->name);
				in->fl |= F[i].bit;
				p->pos++;
				found = true;
				break;
			}
		if (!found)
			PERR(p, t, "unknown flag `%.*s`", (int)t->len, t->s);
	}
}

/* ---------------- instructions ---------------- */

/* the suffix of an op token: "slt" of `icmp.slt` (NULL when there is none) */
static void split_op(const tok *t, const char **nm_, size_t *nl, const char **sfx, size_t *sl)
{
	const char *d = memchr(t->s, '.', t->len);

	*nm_ = t->s;
	if (d) {
		*nl = (size_t)(d - t->s);
		*sfx = d + 1;
		*sl = t->len - *nl - 1;
	} else {
		*nl = t->len;
		*sfx = NULL;
		*sl = 0;
	}
}

static void parse_mem_suffix(ps *p, tok *t, const char *sfx, size_t sl, vir_ins *in, bool sign_ok, bool sign_req)
{
	uint32_t w = 0;
	size_t i = 0;

	if (!sfx || !sl || sfx[0] < '1' || sfx[0] > '8')
		PERR(p, t, "expected a width suffix (.1 .2 .4 .8) on %s", vir_optab[in->op].name);
	w = (uint32_t)(sfx[0] - '0');
	i = 1;
	if (w != 1 && w != 2 && w != 4 && w != 8)
		PERR(p, t, "the width must be 1, 2, 4 or 8");
	in->fl |= (uint16_t)(w == 1 ? 0 : w == 2 ? 1 : w == 4 ? 2 : 3);
	if (i < sl && (sfx[i] == 's' || sfx[i] == 'u')) {
		if (!sign_ok)
			PERR(p, t, "%s has no sign suffix", vir_optab[in->op].name);
		if (sfx[i] == 's')
			in->fl |= VIF_MS;
		i++;
	} else if (sign_req) {
		PERR(p, t, "expected `s` or `u` after the width");
	}
	if (i != sl)
		PERR(p, t, "malformed suffix");
}

/* operand list `v1, 5:i64` into the extra words [off, off+n) (a vector of pops first) */
static void parse_oplist(ps *p, uint32_t term, uint32_t mode, uint32_t aux, uint32_t *npops)
{
	/* the pops are collected in scratch, then the caller reserves the words */
	p->scratch.n = 0;
	if (!(term == 1 ? is_p(cur(p), ')') : term == 2 ? cur(p)->k == T_NL || cur(p)->k == T_EOF : false))
		for (;;) {
			PV_PUSH(p->scratch, parse_operand(p));
			if (!accept_p(p, ','))
				break;
		}
	(void)mode;
	(void)aux;
	*npops = p->scratch.n;
}

static void parse_instr(ps *p, uint32_t rty)
{
	tok *ot = next(p);
	const char *nmp, *sfx;
	size_t nl, sl;
	const vir_opinfo *oi;
	vir_ins in;
	ppos pos = { ot->line, ot->col, 0, 0, 0 };

	split_op(ot, &nmp, &nl, &sfx, &sl);
	oi = vir_opfind(nmp, nl);
	if (!oi)
		PERR(p, ot, "unknown instruction `%.*s`", (int)nl, nmp);
	memset(&in, 0, sizeof(in));
	in.op = oi->op;
	in.ty = (uint8_t)rty;
	if (!p->infn || !p->pb.n)
		PERR(p, ot, "an instruction must be inside a block");
	if (sfx && oi->form != FM_CC && oi->form != FM_EXT && oi->form != FM_LOAD && oi->form != FM_STORE &&
	    oi->form != FM_ATOM)
		PERR(p, ot, "%s takes no suffix", oi->name);
	switch (oi->form) {
	case FM_CONST: {	/* a named constant: it becomes an entry of the constant region as written */
		cdef d;
		uint32_t po = parse_operand(p);

		memset(&d, 0, sizeof(d));
		d.in = in;
		d.pop = po;
		d.name = ~0u;
		parse_flags(p, &d.in);
		expect_nl(p);
		if (d.in.fl)
			PERR(p, ot, "a constant has no flags");
		PV_PUSH(p->cdefs, d);
		return;	/* the caller records the name */
	}
	case FM_BPARAM:	/* normally written in the block header; here it is a stray one (verifier rule S4) */
	case FM_NONE:
		break;
	case FM_CC: {
		uint32_t cc = ~0u;

		if (!sfx)
			PERR(p, ot, "expected a condition code suffix");
		if (oi->op == VO_ICMP) {
			for (uint32_t i = 0; i < 10; i++)
				if (strlen(vir_icc[i]) == sl && !memcmp(vir_icc[i], sfx, sl))
					cc = i;
		} else {
			for (uint32_t i = 0; i < 8; i++)
				if (strlen(vir_fcc[i]) == sl && !memcmp(vir_fcc[i], sfx, sl))
					cc = i;
		}
		if (cc == ~0u)
			PERR(p, ot, "unknown condition code `%.*s`", (int)sl, sfx);
		in.fl = (uint16_t)cc;
		in.a = fld_val(p, 0);
		expect_p(p, ',');
		in.b = fld_val(p, 1);
		break;
	}
	case FM_EXT: {
		uint32_t w;

		if (!sfx || sl < 2 || sfx[0] < '1' || sfx[0] > '8' || (sfx[1] != 's' && sfx[1] != 'u') || sl != 2)
			PERR(p, ot, "expected ext.<width><s|u>");
		w = (uint32_t)(sfx[0] - '0');
		if (w != 1 && w != 2 && w != 4 && w != 8)
			PERR(p, ot, "the width must be 1, 2, 4 or 8");
		in.fl = (uint16_t)((w == 1 ? 0 : w == 2 ? 1 : w == 4 ? 2 : 3) | (sfx[1] == 's' ? VIF_MS : 0));
		in.a = fld_val(p, 0);
		break;
	}
	case FM_LOAD:
		parse_mem_suffix(p, ot, sfx, sl, &in, true, false);
		in.a = fld_val(p, 0);
		break;
	case FM_STORE:
		parse_mem_suffix(p, ot, sfx, sl, &in, false, false);
		in.a = fld_val(p, 0);
		expect_p(p, ',');
		in.b = fld_val(p, 1);
		break;
	case FM_ATOM:
		parse_mem_suffix(p, ot, sfx, sl, &in, false, false);
		for (uint32_t k = 0; k < 3 && oi->k[k] != OK_NONE; k++) {
			uint32_t v;

			if (k)
				expect_p(p, ',');
			v = fld_val(p, k);
			if (k == 0) in.a = v; else if (k == 1) in.b = v; else in.c = v;
		}
		break;
	case FM_MEMCPY:
		in.a = fld_val(p, 0);
		expect_p(p, ',');
		in.b = fld_val(p, 1);
		expect_p(p, ',');
		in.c = fld_val(p, 2);
		break;
	case FM_PARAM: {
		tok *g = cur(p);

		in.a = expect_u32(p, "the group", 0xffff);
		(void)g;
		expect_p(p, '.');
		in.b = expect_u32(p, "the piece", 0xffff);
		break;
	}
	case FM_TLS: {
		tok *s = expect_k(p, T_SYM, "a symbol (@name)");

		in.a = sym_ref(p, s);
		expect_p(p, ',');
		in.b = expect_u32(p, "the model", UINT32_MAX);
		break;
	}
	case FM_VAARG: {
		uint32_t w0, w1, g[2];

		in.a = fld_val(p, 0);
		expect_p(p, ',');
		w0 = parse_group(p, &w1);
		g[0] = w0;
		g[1] = w1;
		in.b = vir_extra(&p->f, g, 2);
		break;
	}
	case FM_CALL: {
		tok *st;
		bool isnew;
		uint32_t e, si, n, off;

		in.a = fld_val(p, 0);
		st = expect_k(p, T_SIG, "a signature (%name)");
		e = nm_get(&p->signame, st->s, st->len, &isnew);
		if (isnew || !p->signame.e.v[e].st)
			PERR(p, st, "undeclared signature %%%.*s", (int)st->len, st->s);
		si = p->signame.e.v[e].id;
		if (p->sigoff.v[si] == ~0u)
			p->sigoff.v[si] = vir_extra(&p->f, &p->u->sigw[p->u->sig[si].off], p->u->sig[si].n);
		in.b = p->sigoff.v[si];
		expect_p(p, '(');
		parse_oplist(p, 1, WM_LIST, 0, &n);
		expect_p(p, ')');
		{
			uint32_t *w = calloc((size_t)n + 1, 4);

			if (!w)
				abort();
			w[0] = n;
			off = vir_extra(&p->f, w, n + 1);
			free(w);
		}
		in.c = off;
		for (uint32_t i = 0; i < n; i++)
			list_val(p, off + 1 + i, WM_LIST, 0, i, p->scratch.v[i]);
		break;
	}
	case FM_ASM: case FM_ASMGOTO: {
		tok *tt = expect_k(p, T_ASM, "an asm template (!name)");
		bool isnew;
		uint32_t e = nm_get(&p->asmname, tt->s, tt->len, &isnew), no, ni, off;
		PV(uint32_t) w = { 0 };
		PV(uint32_t) vs = { 0 };

		if (isnew || !p->asmname.e.v[e].st)
			PERR(p, tt, "undeclared asm template !%.*s", (int)tt->len, tt->s);
		in.a = p->asmname.e.v[e].id;
		expect_kw(p, "outs");
		expect_p(p, '(');
		if (!is_p(cur(p), ')'))
			for (;;) {
				PV_PUSH(w, expect_u32(p, "the operand index", UINT32_MAX));
				if (!accept_p(p, ','))
					break;
			}
		expect_p(p, ')');
		no = w.n;
		expect_kw(p, "ins");
		expect_p(p, '(');
		ni = 0;
		if (!is_p(cur(p), ')'))
			for (;;) {
				PV_PUSH(w, expect_u32(p, "the operand index", UINT32_MAX));
				PV_PUSH(w, 0);
				PV_PUSH(vs, parse_operand(p));
				ni++;
				if (!accept_p(p, ','))
					break;
			}
		expect_p(p, ')');
		{
			uint32_t *all = malloc(((size_t)w.n + 2) * 4);

			if (!all)
				abort();
			all[0] = no;
			all[1] = ni;
			if (w.n)
				memcpy(&all[2], w.v, (size_t)w.n * 4);
			off = vir_extra(&p->f, all, w.n + 2);
			free(all);
		}
		in.b = off;
		for (uint32_t i = 0; i < ni; i++)
			list_val(p, off + 2 + no + 2 * i + 1, WM_LIST, 0, i, vs.v[i]);
		PV_FREE(w);
		PV_FREE(vs);
		if (oi->form == FM_ASMGOTO) {
			uint32_t ft, nl2 = 0, loff;
			PV(uint32_t) ls = { 0 };
			tok *ftt;

			expect_k(p, T_ARROW, "`->`");
			ftt = cur(p);
			ft = block_ref(p);
			expect_p(p, '[');
			PV(tok *) lts = { 0 };

			if (!is_p(cur(p), ']'))
				for (;;) {
					tok *bt = cur(p);

					PV_PUSH(ls, block_ref(p));
					PV_PUSH(lts, bt);
					nl2++;
					if (!accept_p(p, ','))
						break;
				}
			expect_p(p, ']');
			{
				uint32_t *all = calloc((size_t)nl2 + 2, 4);

				if (!all)
					abort();
				all[0] = nl2;
				loff = vir_extra(&p->f, all, nl2 + 2);
				free(all);
			}
			in.c = loff;
			list_blk(p, loff + 1, ft, ftt);
			for (uint32_t i = 0; i < nl2; i++)
				list_blk(p, loff + 2 + i, ls.v[i], lts.v[i]);
			PV_FREE(ls);
			PV_FREE(lts);
		}
		break;
	}
	case FM_BR: {
		tok *bt = cur(p);
		uint32_t bn = block_ref(p), n = 0, off = 0;

		fixup x = { FX_BLK, 0, 0, p->B.n, 0, bn, 0, 0, bt->line, bt->col };

		add_fix(p, x);
		if (accept_p(p, '(')) {
			parse_oplist(p, 1, WM_BR, bn, &n);
			expect_p(p, ')');
			if (n) {
				off = vir_extra(&p->f, p->scratch.v, n);	/* the words hold pop indices for now */
				for (uint32_t i = 0; i < n; i++)
					list_val(p, off + i, WM_BR, bn, i, p->scratch.v[i]);
			}
		}
		in.c = off;
		pos.cnt = n;
		break;
	}
	case FM_SWITCH: {
		uint32_t bd;
		tok *bt;

		in.a = fld_val(p, 0);
		expect_p(p, ',');
		bt = cur(p);
		bd = block_ref(p);
		{
			fixup x = { FX_BLK, 0, 1, p->B.n, 0, bd, 0, 0, bt->line, bt->col };

			add_fix(p, x);
		}
		expect_p(p, '[');
		pos.c0 = p->cases.n;
		if (!is_p(cur(p), ']'))
			for (;;) {
				pcase c;
				bool n1, n2;
				tok *ct = cur(p);

				c.lo = expect_int(p, true, &n1);
				c.hi = c.lo;
				if (cur(p)->k == T_DOTDOT) {
					p->pos++;
					c.hi = expect_int(p, true, &n2);
					(void)n2;
				}
				(void)n1;
				expect_p(p, ':');
				c.line = ct->line;
				c.col = ct->col;
				c.bn = block_ref(p);
				if (p->cases.n - pos.c0 >= VIR_MAX_SWITCH)
					PERR(p, ct, "too many switch cases");
				PV_PUSH(p->cases, c);
				if (!accept_p(p, ','))
					break;
			}
		expect_p(p, ']');
		pos.c1 = p->cases.n;
		break;
	}
	case FM_IBR: {
		uint32_t n = 0, off;
		PV(uint32_t) ls = { 0 };
		PV(tok *) lts = { 0 };

		in.a = fld_val(p, 0);
		expect_p(p, '[');
		if (!is_p(cur(p), ']'))
			for (;;) {
				tok *bt = cur(p);

				PV_PUSH(ls, block_ref(p));
				PV_PUSH(lts, bt);
				n++;
				if (!accept_p(p, ','))
					break;
			}
		expect_p(p, ']');
		{
			uint32_t *all = calloc((size_t)n + 1, 4);

			if (!all)
				abort();
			all[0] = n;
			off = vir_extra(&p->f, all, n + 1);
			free(all);
		}
		in.c = off;
		for (uint32_t i = 0; i < n; i++)
			list_blk(p, off + 1 + i, ls.v[i], lts.v[i]);
		PV_FREE(ls);
		PV_FREE(lts);
		break;
	}
	case FM_RET: {
		uint32_t n, off;

		parse_oplist(p, 2, WM_LIST, 0, &n);
		{
			uint32_t *all = calloc((size_t)n + 1, 4);

			if (!all)
				abort();
			all[0] = n;
			off = vir_extra(&p->f, all, n + 1);
			free(all);
		}
		in.c = off;
		for (uint32_t i = 0; i < n; i++)
			list_val(p, off + 1 + i, WM_LIST, 0, i, p->scratch.v[i]);
		break;
	}
	default: {	/* FM_STD: the operands the table lists, in order */
		uint32_t nop = 0;

		for (uint32_t k = 0; k < 3; k++) {
			uint32_t kd = oi->k[k], v = 0;

			if (kd == OK_NONE)
				continue;
			if (nop++)
				expect_p(p, ',');
			if (kd == OK_IMM)
				v = expect_u32(p, "the immediate", UINT32_MAX);
			else if (kd == OK_BLK)
				v = fld_blk(p, k);
			else
				v = fld_val(p, k);
			if (k == 0) in.a = v; else if (k == 1) in.b = v; else in.c = v;
		}
		break;
	}
	}
	parse_flags(p, &in);
	expect_nl(p);
	PV_PUSH(p->B, in);
	PV_PUSH(p->bp, pos);
	p->pb.v[p->pb.n - 1].n++;
}

/* ---------------- function ---------------- */

static void fn_reset(ps *p)
{
	nm_reset(&p->vals);
	nm_reset(&p->blks);
	nm_reset(&p->slots);
	p->B.n = p->bp.n = p->pb.n = p->pops.n = p->fix.n = p->cases.n = p->cdefs.n = 0;
	p->sigoff.n = 0;
	for (uint32_t i = 0; i < p->u->nsig; i++)
		PV_PUSH(p->sigoff, ~0u);
	p->scratch.n = 0;
}

static void define_value(ps *p, const tok *t, uint32_t st, uint32_t id)
{
	bool isnew;
	uint32_t e = nm_get(&p->vals, t->s, t->len, &isnew);
	nent *n;

	n = &p->vals.e.v[e];
	if (isnew) {
		n->line = t->line;
		n->col = t->col;
	}
	if (n->st)
		PERR(p, t, "value %.*s defined twice", (int)t->len, t->s);
	n->st = st;
	n->id = id;
}

static void parse_block_header(ps *p)
{
	tok *bt = next(p);
	bool isnew;
	uint32_t e = nm_get(&p->blks, bt->s, bt->len, &isnew);
	pblk b = { p->B.n, 0, 0, 0, e };

	if (isnew) {
		p->blks.e.v[e].line = bt->line;
		p->blks.e.v[e].col = bt->col;
	}
	if (p->blks.e.v[e].st)
		PERR(p, bt, "block %.*s defined twice", (int)bt->len, bt->s);
	p->blks.e.v[e].st = 1;
	p->blks.e.v[e].id = p->pb.n;
	if (bt->len < 2 || bt->s[0] != 'b')
		PERR(p, bt, "a block is named bN");
	PV_PUSH(p->pb, b);
	if (accept_p(p, '(')) {
		for (;;) {
			tok *vt = cur(p);
			vir_ins in;
			ppos pos = { vt->line, vt->col, 0, 0, 0 };

			check_value_name(p, vt);
			p->pos++;
			expect_p(p, ':');
			memset(&in, 0, sizeof(in));
			in.op = VO_BPARAM;
			in.ty = (uint8_t)expect_type(p);
			define_value(p, vt, 1, p->B.n);
			PV_PUSH(p->B, in);
			PV_PUSH(p->bp, pos);
			p->pb.v[p->pb.n - 1].n++;
			p->pb.v[p->pb.n - 1].nparam++;
			if (!accept_p(p, ','))
				break;
		}
		expect_p(p, ')');
	}
	for (;;) {
		tok *t = cur(p);

		if (is_id(t, "loop"))
			p->pb.v[p->pb.n - 1].fl |= VB_LOOP;
		else if (is_id(t, "addr"))
			p->pb.v[p->pb.n - 1].fl |= VB_ADDR;
		else if (is_id(t, "cold"))
			p->pb.v[p->pb.n - 1].fl |= VB_COLD;
		else
			break;
		p->pos++;
	}
	expect_p(p, ':');
	expect_nl(p);
}

/* the type of a value operand, or 0 when it is a constant without a suffix */
static uint32_t pop_type(ps *p, const pop *o)
{
	if (o->kind == PK_NAME) {
		const nent *e = &p->vals.e.v[o->id];

		if (!e->st)
			perr_at(p, o->line, o->col, "undefined value %.*s", (int)e->n, e->s);
		return e->st == 1 ? p->B.v[e->id].ty : p->cdefs.v[e->id].in.ty;
	}
	return o->ty == 0xff ? 0 : o->ty;
}

/* the constant instruction for a pop of a given type; vir_const or vir_const_raw */
static uint32_t make_const(ps *p, const pop *o, uint32_t ty, bool raw)
{
	uint32_t op, a = 0, b = 0, c = 0, v;
	uint64_t x;

	if (!ty)
		perr_at(p, o->line, o->col, "a `:type` suffix is required here");
	switch (o->kind) {
	case PK_INT:
		if (ty != VT_I32 && ty != VT_I64 && ty != VT_PTR)
			perr_at(p, o->line, o->col, "an integer constant needs an integer or pointer type, not %s", vir_tyname[ty]);
		op = VO_ICONST;
		x = o->v;
		if (ty == VT_I32 && (o->neg ? (int64_t)x < INT32_MIN : x > UINT32_MAX))
			perr_at(p, o->line, o->col, "constant out of range for i32");
		if (ty == VT_I32 && o->neg)
			x &= 0xffffffffu;
		b = (uint32_t)x;
		c = ty == VT_I32 ? 0 : (uint32_t)(x >> 32);
		break;
	case PK_FLT: case PK_BITS: {
		if (ty != VT_F32 && ty != VT_F64)
			perr_at(p, o->line, o->col, "a floating-point constant needs f32 or f64, not %s", vir_tyname[ty]);
		op = VO_FCONST;
		x = o->v;
		if (o->kind == PK_FLT && ty == VT_F32) {
			double d;
			float fl;

			memcpy(&d, &x, 8);
			fl = (float)d;
			if ((double)fl != d)
				perr_at(p, o->line, o->col, "not exactly representable as f32");
			x = 0;
			memcpy(&x, &fl, 4);
		}
		if (ty == VT_F32 && x > UINT32_MAX)
			perr_at(p, o->line, o->col, "an f32 pattern has 32 bits");
		b = (uint32_t)x;
		c = ty == VT_F32 ? 0 : (uint32_t)(x >> 32);
		break;
	}
	case PK_SYM: case PK_SLOT:
		if (ty != VT_PTR)
			perr_at(p, o->line, o->col, "an address constant has type ptr, not %s", vir_tyname[ty]);
		op = o->kind == PK_SYM ? VO_SYMADDR : VO_SLOTADDR;
		a = o->id;
		b = (uint32_t)(uint64_t)o->add;
		c = (uint32_t)((uint64_t)o->add >> 32);
		break;
	default:	/* PK_LABEL */
		if (ty != VT_PTR)
			perr_at(p, o->line, o->col, "an address constant has type ptr, not %s", vir_tyname[ty]);
		op = VO_LABELADDR;
		a = p->blks.e.v[o->id].id;
		break;
	}
	v = raw ? vir_const_raw(&p->f, op, ty, a, b, c) : vir_const(&p->f, op, ty, a, b, c);
	if (!v)
		perr_at(p, o->line, o->col, "function too large");
	return v;
}

static void finish_function(ps *p, const tok *close)
{
	vir_fn *f = &p->f;
	uint32_t *cid, nconst, nb = p->pb.n;
	PV(uint32_t) pc = { 0 };	/* the constant made for each fixup's pop (or 0) */

	if (!nb)
		perr_at(p, close->line, close->col, "a function needs at least one block");
	/* every block mentioned is defined */
	for (uint32_t i = 0; i < p->blks.e.n; i++)
		if (!p->blks.e.v[i].st)
			perr_at(p, p->blks.e.v[i].line, p->blks.e.v[i].col, "undefined block %.*s", (int)p->blks.e.v[i].n,
				p->blks.e.v[i].s);
	cid = calloc((size_t)p->cdefs.n + 1, 4);
	if (!cid)
		abort();
	/* constants written as instructions: in text order, not unified */
	for (uint32_t i = 0; i < p->cdefs.n; i++) {
		cdef *d = &p->cdefs.v[i];
		const pop *o = &p->pops.v[d->pop];

		cid[i] = make_const(p, o, d->in.ty, true);
		if (d->in.op != f->ins[cid[i]].op)
			perr_at(p, o->line, o->col, "a constant of this kind is written %s",
				vir_optab[f->ins[cid[i]].op].name);
	}
	/* the constants operands need, with the type their position gives (11.5) */
	for (uint32_t i = 0; i < p->fix.n; i++) {
		fixup *x = &p->fix.v[i];
		const pop *o;
		uint32_t ty, k, op, ity;

		PV_PUSH(pc, 0);
		if (x->kind != FX_VAL)
			continue;
		o = &p->pops.v[x->pop];
		if (o->kind == PK_NAME) {
			pop_type(p, o);	/* reports an undefined name */
			continue;
		}
		k = x->k;
		op = p->B.v[k].op;
		ity = p->B.v[k].ty;
		ty = o->ty == 0xff ? 0 : o->ty;
		if (!ty && x->mode == WM_POS) {
			ty = vir_opnd_fixed(op, ity, x->fld);
			if (!ty && (op == VO_ICMP || op == VO_FCMP)) {	/* the other operand fixes it */
				for (uint32_t j = 0; j < p->fix.n; j++) {
					fixup *y = &p->fix.v[j];

					if (j != i && y->kind == FX_VAL && y->k == k && y->mode == WM_POS && y->fld != x->fld)
						ty = pop_type(p, &p->pops.v[y->pop]);
				}
			}
		} else if (!ty && x->mode == WM_BR) {
			uint32_t tb = p->blks.e.v[x->aux].id;

			if (x->idx < p->pb.v[tb].nparam)
				ty = p->B.v[p->pb.v[tb].first + x->idx].ty;
		}
		if (!ty && (o->kind == PK_SYM || o->kind == PK_SLOT || o->kind == PK_LABEL))
			ty = VT_PTR;	/* an address is a pointer, whatever the position */
		pc.v[i] = make_const(p, o, ty, false);
	}
	nconst = f->nins;
	/* the values: constants first, then the block instructions in text order */
	for (uint32_t i = 0; i < p->fix.n; i++) {
		fixup *x = &p->fix.v[i];
		uint32_t v;
		uint32_t *dst;

		if (x->kind == FX_VAL) {
			const pop *o = &p->pops.v[x->pop];

			if (o->kind == PK_NAME) {
				const nent *e = &p->vals.e.v[o->id];

				v = e->st == 1 ? nconst + e->id : cid[e->id];
			} else {
				v = pc.v[i];
			}
		} else {
			v = p->blks.e.v[x->pop].id;
		}
		if (x->fld == 3) {
			dst = &f->extra[x->at];
		} else {
			vir_ins *in = &p->B.v[x->k];

			dst = x->fld == 0 ? &in->a : x->fld == 1 ? &in->b : &in->c;
		}
		*dst = v;
	}
	PV_FREE(pc);
	free(cid);
	/* br: the arguments are the target's parameters, exactly (the format holds no count) */
	for (uint32_t k = 0; k < p->B.n; k++) {
		const vir_ins *in = &p->B.v[k];
		const ppos *ps_ = &p->bp.v[k];

		if (in->op == VO_BR && in->a < nb && ps_->cnt != p->pb.v[in->a].nparam)
			perr_at(p, ps_->line, ps_->col, "br passes %u arguments, block b%u takes %u", ps_->cnt, in->a,
				p->pb.v[in->a].nparam);
		if (in->op == VO_SWITCH) {	/* the case list, now that the operand's type is known */
			uint32_t ty = in->a < nconst ? f->ins[in->a].ty : p->B.v[in->a - nconst].ty;
			uint32_t n = ps_->c1 - ps_->c0, st = ty == VT_I64 ? 5 : 3;
			uint32_t *w;

			if (ty != VT_I32 && ty != VT_I64)
				perr_at(p, ps_->line, ps_->col, "a switch operand is i32 or i64");
			w = malloc(((size_t)n * st + 1) * 4);
			if (!w)
				abort();
			w[0] = n;
			for (uint32_t i = 0; i < n; i++) {
				const pcase *c = &p->cases.v[ps_->c0 + i];
				uint32_t *q = &w[1 + (size_t)i * st];

				if (ty == VT_I32) {
					if ((int64_t)c->lo < INT32_MIN || (int64_t)c->hi < INT32_MIN ||
					    (c->lo > UINT32_MAX && (int64_t)c->lo >= 0) || (c->hi > UINT32_MAX && (int64_t)c->hi >= 0)) {
						free(w);
						perr_at(p, c->line, c->col, "case value out of range for i32");
					}
					q[0] = (uint32_t)c->lo;
					q[1] = (uint32_t)c->hi;
				} else {
					q[0] = (uint32_t)c->lo;
					q[1] = (uint32_t)(c->lo >> 32);
					q[2] = (uint32_t)c->hi;
					q[3] = (uint32_t)(c->hi >> 32);
				}
				q[st - 1] = p->blks.e.v[c->bn].id;
			}
			p->B.v[k].c = vir_extra(f, w, n * st + 1);
			free(w);
		}
	}
	if (vir_failed() || nconst + p->B.n > VIR_MAX_INS)
		perr_at(p, close->line, close->col, "function too large");
	if (p->B.n)
		memcpy(&f->ins[nconst], p->B.v, (size_t)p->B.n * sizeof(vir_ins));
	f->nins = nconst + p->B.n;
	f->nconst = nconst;
	for (uint32_t i = 0; i < nb; i++) {
		vir_blk_add(f, 0);
		f->blk[i].first = nconst + p->pb.v[i].first;
		f->blk[i].n = p->pb.v[i].n;
		f->blk[i].nparam = (uint16_t)p->pb.v[i].nparam;
		f->blk[i].fl = (uint16_t)p->pb.v[i].fl;
	}
	if (vir_failed())
		perr_at(p, close->line, close->col, "function too large");
}

static uint32_t fn_sig(ps *p, const tok *t)
{
	bool isnew;
	uint32_t e, si;

	if (t->k != T_SIG)
		PERR(p, t, "expected the function's signature (%%name)");
	e = nm_get(&p->signame, t->s, t->len, &isnew);
	if (isnew || !p->signame.e.v[e].st)
		PERR(p, t, "undeclared signature %%%.*s", (int)t->len, t->s);
	si = p->signame.e.v[e].id;
	if (p->sigoff.v[si] == ~0u)
		p->sigoff.v[si] = vir_extra(&p->f, &p->u->sigw[p->u->sig[si].off], p->u->sig[si].n);
	return si;
}

static void parse_func(ps *p)
{
	tok *st, *gt;
	uint32_t fl = 0, sym, si;
	bool slots_open = true;

	next(p);
	st = expect_k(p, T_SYM, "a symbol (@name)");
	define_sym(p, st, VU_FUNC);
	sym = sym_ref(p, st);
	gt = cur(p);
	if (gt->k != T_SIG)
		PERR(p, gt, "expected the function's signature (%%name)");
	p->pos++;
	for (;;) {
		tok *t = cur(p);

		if (is_id(t, "nofp")) fl |= VF_NOFP;
		else if (is_id(t, "fenv")) fl |= VF_FENV;
		else if (is_id(t, "nullinvalid")) fl |= VF_NULLINVALID;
		else if (is_id(t, "noreturn")) fl |= VF_NORETURN;
		else if (is_id(t, "local")) fl |= VF_LOCAL;
		else if (is_id(t, "exposure")) fl |= VF_EXPOSURE;
		else break;
		p->pos++;
	}
	expect_p(p, '{');
	expect_nl(p);
	vir_begin(&p->f, p->u->target, sym, fl);
	p->f.version = p->u->version;
	fn_reset(p);
	p->infn = true;
	si = fn_sig(p, gt);
	p->f.sig = p->sigoff.v[si];
	if (VSIG_VARIADIC(p->u->sigw[p->u->sig[si].off]))
		p->f.fl |= VF_VARIADIC;
	for (;;) {
		tok *t = cur(p);

		if (t->k == T_NL) {
			p->pos++;
			continue;
		}
		if (t->k == T_EOF)
			PERR(p, t, "the function is not closed (`}` expected)");
		if (is_p(t, '}')) {
			p->pos++;
			finish_function(p, t);
			expect_nl(p);
			break;
		}
		if (is_id(t, "slot")) {
			tok *nt, *at;
			uint64_t size, al, sfl = 0;
			int64_t hint = INT64_MIN;
			bool isnew;
			uint32_t e;

			if (!slots_open)
				PERR(p, t, "slots are declared before the first block");
			p->pos++;
			nt = expect_k(p, T_SLOT, "a slot ($N)");
			e = nm_get(&p->slots, nt->s, nt->len, &isnew);
			if (!isnew)
				PERR(p, nt, "slot $%.*s declared twice", (int)nt->len, nt->s);
			expect_kw(p, "size");
			size = expect_int(p, false, NULL);
			expect_kw(p, "align");
			at = cur(p);
			al = expect_int(p, false, NULL);
			if (al > UINT32_MAX)
				PERR(p, at, "alignment too large");
			for (;;) {
				if (is_id(cur(p), "hint")) {
					bool ng;

					p->pos++;
					hint = (int64_t)expect_int(p, true, &ng);
				} else if (is_id(cur(p), "param")) {
					p->pos++;
					sfl |= VS_PARAM;
				} else if (is_id(cur(p), "exposed")) {
					p->pos++;
					sfl |= VS_EXPOSED;
				} else {
					break;
				}
			}
			expect_nl(p);
			p->slots.e.v[e].st = 1;
			p->slots.e.v[e].id = vir_slot_add(&p->f, size, (uint32_t)al, (uint32_t)sfl, hint);
			if (vir_failed())
				PERR(p, nt, "too many slots");
			continue;
		}
		{	/* a block header, or an instruction (with its result) */
			tok *t1 = peek(p, 1), *t2 = peek(p, 2), *t3 = peek(p, 3);
			bool hdr = t->k == T_ID && (is_p(t1, '(') || is_id(t1, "loop") || is_id(t1, "addr") || is_id(t1, "cold") ||
						    (is_p(t1, ':') && (t2->k == T_NL || t2->k == T_EOF)));

			if (hdr && t->k == T_ID && !is_p(t1, '(')) {
				/* `b1 loop:` or `b1:` */
			}
			slots_open = false;
			if (hdr) {
				parse_block_header(p);
				continue;
			}
			if (t->k == T_ID && is_p(t1, ':') && type_of(t2) && is_p(t3, '=')) {	/* vN:T = op ... */
				uint32_t ty = type_of(t2);
				tok *vt = t;
				uint32_t before = p->B.n, cb = p->cdefs.n;

				check_value_name(p, vt);
				p->pos += 4;
				if (cur(p)->k != T_ID)
					PERR(p, cur(p), "expected an instruction");
				parse_instr(p, ty);
				if (p->cdefs.n != cb) {	/* a named constant */
					p->cdefs.v[cb].in.ty = (uint8_t)ty;
					p->cdefs.v[cb].name = vt - p->t.v;
					define_value(p, vt, 2, cb);
				} else if (p->B.n != before) {
					define_value(p, vt, 1, before);
				}
				continue;
			}
			if (t->k != T_ID)
				PERR(p, t, "expected a block, an instruction or `}`");
			if (is_p(t1, ':') && t2->k == T_ID && is_p(t3, '=') && !type_of(t2))
				PERR(p, t2, "expected a type (i32, i64, f32, f64, ptr, multi)");
			if (!vir_opfind(t->s, (size_t)(memchr(t->s, '.', t->len) ? (const char *)memchr(t->s, '.', t->len) - t->s : t->len)) &&
			    t->s[0] == 'b' && (t1->k == T_NL || t1->k == T_EOF))
				PERR(p, t1, "expected `:` after the block name");
			parse_instr(p, VT_VOID);
			if (p->cdefs.n && p->cdefs.v[p->cdefs.n - 1].name == ~0u)
				PERR(p, t, "a constant needs a name (vN:type = ...)");
		}
	}
	p->infn = false;
	if (!p->infn) {
		if (p->f.nblk >= 1)
			vir_fn_save(p->u, &p->f, sym);
	}
}

static void free_ps(ps *p)
{
	PV_FREE(p->t);
	free(p->sbuf);
	nm_free(&p->signame);
	nm_free(&p->asmname);
	nm_free(&p->vals);
	nm_free(&p->blks);
	nm_free(&p->slots);
	PV_FREE(p->B);
	PV_FREE(p->bp);
	PV_FREE(p->pb);
	PV_FREE(p->pops);
	PV_FREE(p->fix);
	PV_FREE(p->cases);
	PV_FREE(p->cdefs);
	PV_FREE(p->sigoff);
	PV_FREE(p->scratch);
}

bool vir_parse(vir_unit *u, const char *src, size_t n, vir_perr *err)
{
	ps p;
	tok *t;

	memset(&p, 0, sizeof(p));
	p.src = src;
	p.u = u;
	p.err = err;
	memset(err, 0, sizeof(*err));
	if (setjmp(p.jb)) {
		free_ps(&p);
		return false;
	}
	lex_all(&p, src, n);
	while (cur(&p)->k == T_NL)
		p.pos++;
	expect_kw(&p, "vir");
	u->version = 0;
	{
		tok *vt = cur(&p);
		uint64_t v = expect_int(&p, false, NULL);

		if (v > 0xffff)
			PERR(&p, vt, "version out of range");
		u->version = (uint32_t)v;
	}
	expect_nl(&p);
	expect_kw(&p, "target");
	{
		tok *a = cur(&p), *b = a;
		uint32_t tg = 0;

		while (b->k != T_NL && b->k != T_EOF)
			b++;
		if (b == a)
			PERR(&p, a, "expected a target");
		for (uint32_t i = 1; i < VTG_N; i++) {
			const char *nm_ = vir_target_name(i);
			size_t l = (size_t)((b - 1)->s + (b - 1)->len - a->s);

			if (strlen(nm_) == l && !memcmp(nm_, a->s, l))
				tg = i;
		}
		if (!tg)
			PERR(&p, a, "unknown target (x86_64-linux, arm64-linux, arm64-darwin)");
		u->target = tg;
		p.pos = (uint32_t)(b - p.t.v);
	}
	expect_nl(&p);
	for (;;) {
		t = cur(&p);
		if (t->k == T_NL) {
			p.pos++;
			continue;
		}
		if (t->k == T_EOF)
			break;
		if (t->k != T_ID)
			PERR(&p, t, "expected a declaration (sig, asm, data, tdata, extern, func)");
		if (is_id(t, "sig")) {
			parse_sigdecl(&p);
		} else if (is_id(t, "asm")) {
			parse_asmdecl(&p);
		} else if (is_id(t, "data") || is_id(t, "tdata")) {
			parse_datadecl(&p);
		} else if (is_id(t, "extern")) {
			tok *s;

			p.pos++;
			s = expect_k(&p, T_SYM, "a symbol (@name)");
			define_sym(&p, s, VU_EXTERN);
			expect_nl(&p);
		} else if (is_id(t, "func")) {
			parse_func(&p);
		} else {
			PERR(&p, t, "unknown declaration `%.*s`", (int)t->len, t->s);
		}
	}
	for (uint32_t i = 0; i < u->nsym; i++)
		if (u->sym[i].kind == VU_UNDECL)
			perr_at(&p, u->sym[i].line, u->sym[i].col, "undeclared symbol @%s", u->sym[i].name);
	free_ps(&p);
	return true;
}
