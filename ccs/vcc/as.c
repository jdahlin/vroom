/*
 * as.c: GNU as syntax for AArch64, the subset the kernel's .S files use,
 * with LLVM MC's semantics (the reference is clang -c / -cc1as).
 *
 * Text is processed one statement at a time from a stack of frames: the
 * input file, macro instantiations and .rept/.irp expansions.  Expansions
 * are produced as text (exactly as MC does) on a stack region that is
 * released when the frame ends.  Instructions go to a64_asm(); everything
 * symbolic becomes an elf_fixup() resolved at the end.
 */
#include "as.h"
#include "a64.h"
#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAXFR 512
#define MAXCOND 1024
#define MAXSECSTK 256
#define MAXOPS 8

typedef struct { const char *s; uint32_t n; } Sv;

typedef struct {
	Sv name, def;
	uint8_t req, vararg;
} Param;

typedef struct Macro {
	Sv name;
	Param *params; uint32_t np;
	Sv body;
	uint32_t count;
	struct Macro *next;
} Macro;

typedef struct {
	const char *p, *e;
	uint8_t kind;		/* FR_FILE, FR_MACRO, FR_LOOP */
	uint8_t bol;		/* at beginning of a line */
	uint32_t ncond;
	size_t mark;
	uint32_t line;
} Frame;
enum { FR_FILE, FR_MACRO, FR_LOOP };

typedef struct { uint8_t kind, met, ignore; } Cond;	/* kind: 1 if, 2 elseif, 3 else */

typedef struct { uint32_t num, cur, fwd; } NumLab;

typedef struct { Sv name; Sv reg; } Alias;

typedef struct { uint32_t sym; ElfVal v; } Var;	/* .set with a non-absolute value */

typedef struct { uint8_t *base; size_t used, cap; } Region;

struct As {
	ElfObj *o;
	Frame fr[MAXFR]; int nfr;
	Cond cs[MAXCOND]; int ncs;
	uint32_t cur, prev;	/* current/previous frag */
	struct { uint32_t cur, prev; } ss[MAXSECSTK]; int nss;
	Region xs;		/* expansion text stack */
	Region defs;		/* macro definitions etc. (per run) */
	Region sb;		/* current statement */
	Macro **mtab; uint32_t mmask, mcount;
	NumLab *nl; uint32_t nlmask, nlcount;
	Alias *al; uint32_t nal, alcap;
	Var *vars; uint32_t vmask, vcount;
	void *pool_dummy; struct PoolS *pool; uint32_t npool, poolcap;
	uint32_t ninst;		/* \@ */
	const char *fname;
	char msg[512];
	int err;
};

/* ---------------------------------------------------------------- utilities */

static void *reserve(size_t n)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
	if (p == MAP_FAILED) { perror("vcc: as: mmap"); abort(); }
	return p;
}

static void *rget(Region *r, size_t n)
{
	size_t u = (r->used + 7) & ~(size_t)7;
	if (u + n > r->cap) { fprintf(stderr, "vcc: as: region full\n"); abort(); }
	r->used = u + n;
	return r->base + u;
}

static Sv svdup(As *a, const char *s, size_t n)
{
	char *p = rget(&a->defs, n + 1);
	memcpy(p, s, n); p[n] = 0;
	return (Sv){ p, (uint32_t)n };
}

static uint32_t curline(As *a)
{
	for (int i = a->nfr - 1; i >= 0; i--) if (a->fr[i].kind == FR_FILE) return a->fr[i].line;
	return 0;
}

static int err(As *a, const char *fmt, ...)
{
	if (a->err) return -1;
	a->err = 1;
	int k = snprintf(a->msg, sizeof a->msg, "%s:%u: error: ", a->fname, curline(a));
	va_list ap; va_start(ap, fmt);
	vsnprintf(a->msg + k, sizeof a->msg - k, fmt, ap);
	va_end(ap);
	return -1;
}

static inline int isidc(int c) { return isalnum(c) || c == '_' || c == '.' || c == '$'; }
static inline int isids(int c) { return isalpha(c) || c == '_' || c == '.' || c == '$'; }
static inline const char *ws(const char *p, const char *e) { while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' || *p == '\v')) p++; return p; }
static inline int sveq(Sv a, const char *s) { size_t n = strlen(s); return a.n == n && !memcmp(a.s, s, n); }
static inline int sveqi(const char *p, size_t n, const char *s)
{
	size_t m = strlen(s);
	if (m != n) return 0;
	for (size_t i = 0; i < n; i++) if (tolower((unsigned char)p[i]) != s[i]) return 0;
	return 1;
}

static uint32_t hsv(const char *s, size_t n)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
	return h;
}

/* ---------------------------------------------------------------- symbols */

static uint32_t sym(As *a, const char *s, size_t n)
{
	uint32_t y = elf_sym(a->o, s, (uint32_t)n);
	ElfSym *p = elf_symp(a->o, y);
	if (!p->flags && n >= 2 && s[0] == '.' && s[1] == 'L') p->flags |= ESYM_TEMP;
	return y;
}

static uint32_t tmpsym(As *a)
{
	uint32_t y = elf_sym_anon(a->o, "", 0);
	elf_symp(a->o, y)->flags |= ESYM_TEMP;
	return y;
}

static uint32_t here(As *a)
{
	uint32_t y = tmpsym(a);
	elf_define(a->o, y, a->cur, elf_frag(a->o, a->cur)->size);
	return y;
}

static NumLab *numlab(As *a, uint32_t num)
{
	if (a->nlcount * 2 >= a->nlmask) {
		uint32_t nm = a->nlmask * 2 + 1;
		NumLab *t = rget(&a->defs, sizeof *t * (nm + 1));
		memset(t, 0, sizeof *t * (nm + 1));
		for (uint32_t i = 0; i <= a->nlmask; i++) {
			if (!a->nl[i].cur && !a->nl[i].fwd) continue;
			uint32_t j = (a->nl[i].num * 2654435761u) & nm;
			while (t[j].cur || t[j].fwd) j = (j + 1) & nm;
			t[j] = a->nl[i];
		}
		a->nl = t; a->nlmask = nm;
	}
	uint32_t j = (num * 2654435761u) & a->nlmask;
	while (a->nl[j].cur || a->nl[j].fwd) {
		if (a->nl[j].num == num) return &a->nl[j];
		j = (j + 1) & a->nlmask;
	}
	a->nl[j].num = num; a->nlcount++;
	return &a->nl[j];
}

static Var *var_slot(As *a, uint32_t y, int create)
{
	if (create && a->vcount * 2 >= a->vmask) {
		uint32_t nm = a->vmask * 2 + 1;
		Var *t = rget(&a->defs, sizeof *t * (nm + 1));
		memset(t, 0, sizeof *t * (nm + 1));
		for (uint32_t i = 0; i <= a->vmask; i++) {
			if (!a->vars[i].sym) continue;
			uint32_t j = (a->vars[i].sym * 2654435761u) & nm;
			while (t[j].sym) j = (j + 1) & nm;
			t[j] = a->vars[i];
		}
		a->vars = t; a->vmask = nm;
	}
	uint32_t j = (y * 2654435761u) & a->vmask;
	while (a->vars[j].sym) {
		if (a->vars[j].sym == y) return &a->vars[j];
		j = (j + 1) & a->vmask;
	}
	if (!create) return 0;
	a->vars[j].sym = y; a->vcount++;
	return &a->vars[j];
}

/* ---------------------------------------------------------------- expressions */

typedef ElfVal Val;

static int is_abs(As *a, Val *v)
{
	(void)a;
	return !v->add && !v->sub;
}

/* fold add - sub when both are in the same frag (or absolute) */
static void fold(As *a, Val *v)
{
	ElfObj *o = a->o;
	if (v->add) {
		ElfSym *y = elf_symp(o, v->add);
		if (y->flags & ESYM_ABS) { v->c += (int64_t)y->value; v->add = 0; }
	}
	if (v->sub) {
		ElfSym *y = elf_symp(o, v->sub);
		if (y->flags & ESYM_ABS) { v->c -= (int64_t)y->value; v->sub = 0; }
	}
	if (v->add && v->sub) {
		ElfSym *x = elf_symp(o, v->add), *y = elf_symp(o, v->sub);
		if (v->add == v->sub) { v->add = v->sub = 0; return; }
		if ((x->flags & ESYM_DEFINED) && (y->flags & ESYM_DEFINED) && x->frag == y->frag) {
			v->c += (int64_t)x->value - (int64_t)y->value;
			v->add = v->sub = 0;
		}
	}
}

typedef struct { As *a; const char *p, *e; } Lx;

static int expr(Lx *l, Val *v);

static int parse_number(Lx *l, Val *v)
{
	const char *p = l->p, *e = l->e;
	uint64_t x = 0;
	if (p + 1 < e && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		p += 2;
		const char *s = p;
		while (p < e && isxdigit((unsigned char)*p)) { x = x * 16 + (uint64_t)(isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10)); p++; }
		if (p == s) return err(l->a, "invalid hexadecimal number");
	} else if (p + 2 < e && p[0] == '0' && (p[1] == 'b' || p[1] == 'B') && (p[2] == '0' || p[2] == '1')) {
		p += 2;
		while (p < e && (*p == '0' || *p == '1')) x = x * 2 + (uint64_t)(*p++ - '0');
	} else {
		const char *s = p;
		while (p < e && isdigit((unsigned char)*p)) p++;
		/* local label reference */
		if (p < e && (*p == 'b' || *p == 'f') && (p + 1 >= e || !isidc((unsigned char)p[1]))) {
			uint32_t num = 0;
			for (const char *q = s; q < p; q++) num = num * 10 + (uint32_t)(*q - '0');
			NumLab *nl = numlab(l->a, num);
			uint32_t y;
			if (*p == 'b') {
				if (!nl->cur) return err(l->a, "directional label undefined");
				y = nl->cur;
			} else {
				if (!nl->fwd) nl->fwd = tmpsym(l->a);
				y = nl->fwd;
			}
			l->p = p + 1;
			*v = (Val){ y, 0, 0 };
			fold(l->a, v);
			return 0;
		}
		if (s[0] == '0' && p - s > 1) {	/* octal */
			for (const char *q = s + 1; q < p; q++) x = x * 8 + (uint64_t)(*q - '0');
		} else {
			for (const char *q = s; q < p; q++) x = x * 10 + (uint64_t)(*q - '0');
		}
	}
	l->p = p;
	*v = (Val){ 0, 0, (int64_t)x };
	return 0;
}

static int primary(Lx *l, Val *v)
{
	As *a = l->a;
	l->p = ws(l->p, l->e);
	if (l->p >= l->e) return err(a, "unexpected end of expression");
	char c = *l->p;
	if (c == '(') {
		l->p++;
		if (expr(l, v)) return -1;
		l->p = ws(l->p, l->e);
		if (l->p >= l->e || *l->p != ')') return err(a, "expected ')' in expression");
		l->p++;
		return 0;
	}
	if (c == '-' || c == '~' || c == '!' || c == '+') {
		l->p++;
		if (primary(l, v)) return -1;
		if (c == '+') return 0;
		if (c == '-') {
			if (v->add && v->sub) { uint32_t t = v->add; v->add = v->sub; v->sub = t; v->c = -v->c; return 0; }
			if (!is_abs(a, v)) return err(a, "unsupported negation of a symbol");
			v->c = -v->c; return 0;
		}
		if (!is_abs(a, v)) return err(a, "expected absolute expression");
		v->c = c == '~' ? ~v->c : !v->c;
		return 0;
	}
	if (isdigit((unsigned char)c)) return parse_number(l, v);
	if (c == '\'') {
		const char *p = l->p + 1;
		if (p >= l->e) return err(a, "bad character literal");
		int64_t x;
		if (*p == '\\' && p + 1 < l->e) {
			p++;
			switch (*p) { case 'n': x = '\n'; break; case 't': x = '\t'; break; case '0': x = 0; break; default: x = (unsigned char)*p; }
		} else x = (unsigned char)*p;
		p++;
		if (p < l->e && *p == '\'') p++;
		l->p = p;
		*v = (Val){ 0, 0, x };
		return 0;
	}
	if (c == '.' && (l->p + 1 >= l->e || !isidc((unsigned char)l->p[1]))) {
		l->p++;
		*v = (Val){ here(a), 0, 0 };
		return 0;
	}
	if (isids((unsigned char)c) || c == '"') {
		const char *s, *p;
		if (c == '"') {
			s = l->p + 1; p = s;
			while (p < l->e && *p != '"') p++;
			if (p >= l->e) return err(a, "unterminated quoted symbol");
			l->p = p + 1;
		} else {
			s = l->p; p = s;
			while (p < l->e && isidc((unsigned char)*p)) p++;
			l->p = p;
		}
		uint32_t y = sym(a, s, (size_t)(p - s));
		*v = (Val){ y, 0, 0 };
		if (a->vcount) {
			Var *vr = var_slot(a, y, 0);
			if (vr && vr->v.add != (uint32_t)-1) *v = vr->v;
		}
		fold(a, v);
		return 0;
	}
	return err(a, "unknown token in expression");
}

/* GNU precedences as MC uses them */
static int binop(Lx *l, int *op)
{
	const char *p = ws(l->p, l->e), *e = l->e;
	if (p >= e) return 0;
	char c = p[0], d = p + 1 < e ? p[1] : 0;
	int prec = 0, len = 1;
	switch (c) {
	case '&': if (d == '&') { *op = 'A'; prec = 2; len = 2; } else { *op = '&'; prec = 5; } break;
	case '|': if (d == '|') { *op = 'O'; prec = 1; len = 2; } else { *op = '|'; prec = 5; } break;
	case '=': if (d == '=') { *op = 'E'; prec = 3; len = 2; } else return 0; break;
	case '!': if (d == '=') { *op = 'N'; prec = 3; len = 2; } else { *op = '!'; prec = 5; } break;
	case '<':
		if (d == '<') { *op = 'L'; prec = 6; len = 2; }
		else if (d == '=') { *op = 'l'; prec = 3; len = 2; }
		else if (d == '>') { *op = 'N'; prec = 3; len = 2; }
		else { *op = '<'; prec = 3; }
		break;
	case '>':
		if (d == '>') { *op = 'R'; prec = 6; len = 2; }
		else if (d == '=') { *op = 'g'; prec = 3; len = 2; }
		else { *op = '>'; prec = 3; }
		break;
	case '+': *op = '+'; prec = 4; break;
	case '-': *op = '-'; prec = 4; break;
	case '^': *op = '^'; prec = 5; break;
	case '*': *op = '*'; prec = 6; break;
	case '/': *op = '/'; prec = 6; break;
	case '%': *op = '%'; prec = 6; break;
	default: return 0;
	}
	l->p = p + len;
	return prec;
}

static int apply(As *a, int op, Val *x, Val *y)
{
	if (op == '+' || op == '-') {
		Val r = *y;
		if (op == '-') { r.add = y->sub; r.sub = y->add; r.c = -y->c; }
		if ((x->add && r.add) || (x->sub && r.sub)) {
			/* try to cancel: x.add - r.sub etc. handled by fold below */
			Val t = { x->add, r.sub, 0 }, u = { r.add, x->sub, 0 };
			fold(a, &t); fold(a, &u);
			if (t.add && t.sub && u.add && u.sub) return err(a, "expression too complex for a relocation");
			Val res = { 0, 0, x->c + r.c + t.c + u.c };
			if (t.add) res.add = t.add; if (t.sub) res.sub = t.sub;
			if (u.add) { if (res.add) return err(a, "expression too complex"); res.add = u.add; }
			if (u.sub) { if (res.sub) return err(a, "expression too complex"); res.sub = u.sub; }
			*x = res;
			fold(a, x);
			return 0;
		}
		x->add = x->add ? x->add : r.add;
		x->sub = x->sub ? x->sub : r.sub;
		x->c += r.c;
		fold(a, x);
		return 0;
	}
	if (!is_abs(a, x) || !is_abs(a, y)) return err(a, "expected absolute expression");
	int64_t p = x->c, q = y->c, r;
	switch (op) {
	case '*': r = (int64_t)((uint64_t)p * (uint64_t)q); break;
	case '/': if (!q) return err(a, "division by zero"); r = p / q; break;
	case '%': if (!q) return err(a, "division by zero"); r = p % q; break;
	case 'L': r = (int64_t)((uint64_t)p << (q & 63)); break;
	case 'R': r = (int64_t)((uint64_t)p >> (q & 63)); break;
	case '&': r = p & q; break;
	case '|': r = p | q; break;
	case '^': r = p ^ q; break;
	case '!': r = p | ~q; break;
	case 'A': r = p && q; break;
	case 'O': r = p || q; break;
	case 'E': r = p == q ? -1 : 0; break;
	case 'N': r = p != q ? -1 : 0; break;
	case '<': r = p < q ? -1 : 0; break;
	case '>': r = p > q ? -1 : 0; break;
	case 'l': r = p <= q ? -1 : 0; break;
	case 'g': r = p >= q ? -1 : 0; break;
	default: return err(a, "bad operator");
	}
	x->c = r;
	return 0;
}

static int expr_prec(Lx *l, Val *v, int minp)
{
	if (primary(l, v)) return -1;
	for (;;) {
		const char *save = l->p;
		int op, p = binop(l, &op);
		if (!p || p < minp) { l->p = save; return 0; }
		Val r;
		if (expr_prec(l, &r, p + 1)) return -1;
		if (apply(l->a, op, v, &r)) return -1;
	}
}

static int expr(Lx *l, Val *v) { return expr_prec(l, v, 1); }

static int abs_expr(As *a, const char **pp, const char *e, int64_t *out)
{
	Lx l = { a, *pp, e };
	Val v;
	if (expr(&l, &v)) return -1;
	*pp = l.p;
	if (!is_abs(a, &v)) return err(a, "expected absolute expression");
	*out = v.c;
	return 0;
}

static int val_expr(As *a, const char **pp, const char *e, Val *v)
{
	Lx l = { a, *pp, e };
	if (expr(&l, v)) return -1;
	*pp = l.p;
	return 0;
}

/* ---------------------------------------------------------------- frames and statements */

static int push_frame(As *a, const char *p, size_t n, int kind, size_t mark)
{
	if (a->nfr >= MAXFR) return err(a, "macros nested too deeply");
	Frame *f = &a->fr[a->nfr++];
	f->p = p; f->e = p + n; f->kind = (uint8_t)kind; f->bol = 1;
	f->ncond = (uint32_t)a->ncs; f->mark = mark; f->line = 1;
	return 0;
}

static void pop_frame(As *a)
{
	Frame *f = &a->fr[--a->nfr];
	if (f->kind != FR_FILE) a->xs.used = f->mark;
}

/* Copy the next statement of f into a->sb, without comments.  Returns its
 * length; sets *raw to where it started in the frame text. */
static size_t next_stmt(As *a, Frame *f, const char **raw)
{
	const char *p = f->p, *e = f->e;
	char *o = (char *)a->sb.base;
	size_t n = 0, cap = a->sb.cap - 1;
	*raw = p;
	for (;;) {
		if (p >= e) break;
		char c = *p;
		if (c == '\n') { p++; f->line++; f->bol = 1; break; }
		if (f->bol) {
			if (c == ' ' || c == '\t' || c == '\r') { p++; if (n < cap) o[n++] = c; continue; }
			f->bol = 0;
			if (c == '#') {		/* cpp line marker / line comment */
				while (p < e && *p != '\n') p++;
				continue;
			}
		}
		if (c == ';') { p++; break; }
		if (c == '/' && p + 1 < e && p[1] == '/') { while (p < e && *p != '\n') p++; continue; }
		if (c == '/' && p + 1 < e && p[1] == '*') {
			p += 2;
			while (p + 1 < e && !(p[0] == '*' && p[1] == '/')) { if (*p == '\n') f->line++; p++; }
			p += 2;
			if (n < cap) o[n++] = ' ';
			continue;
		}
		if (c == '"') {
			if (n < cap) o[n++] = c;
			p++;
			while (p < e && *p != '"' && *p != '\n') {
				if (*p == '\\' && p + 1 < e) { if (n < cap) o[n++] = *p; p++; }
				if (n < cap) o[n++] = *p;
				p++;
			}
			if (p < e && *p == '"') { if (n < cap) o[n++] = '"'; p++; }
			continue;
		}
		if (n < cap) o[n++] = c;
		p++;
	}
	f->p = p;
	o[n] = 0;
	return n;
}

/* ---------------------------------------------------------------- sections */

static int hasprefix(Sv n, const char *pre)
{
	size_t k = strlen(pre);
	return n.n >= k && !memcmp(n.s, pre, k) && (n.n == k || n.s[k] == '.');
}

static void switch_to(As *a, uint32_t frag)
{
	if (frag != a->cur) a->prev = a->cur;
	a->cur = frag;
}

static uint32_t std_section(As *a, const char *name)
{
	Sv n = { name, (uint32_t)strlen(name) };
	uint32_t type = SHT_PROGBITS;
	uint64_t flags = 0;
	if (sveq(n, ".text")) flags = SHF_ALLOC | SHF_EXECINSTR;
	else if (sveq(n, ".data")) flags = SHF_ALLOC | SHF_WRITE;
	else if (sveq(n, ".bss")) { flags = SHF_ALLOC | SHF_WRITE; type = SHT_NOBITS; }
	else if (sveq(n, ".rodata")) flags = SHF_ALLOC;
	return elf_section(a->o, n.s, n.n, type, flags, 0, 0);
}

static int parse_name(As *a, const char **pp, const char *e, Sv *out)
{
	const char *p = ws(*pp, e);
	if (p < e && *p == '"') {
		const char *s = ++p;
		while (p < e && *p != '"') p++;
		if (p >= e) return err(a, "unterminated string");
		*out = (Sv){ s, (uint32_t)(p - s) };
		*pp = p + 1;
		return 0;
	}
	const char *s = p;
	while (p < e && (isidc((unsigned char)*p) || *p == '-')) p++;
	if (p == s) return err(a, "expected identifier");
	*out = (Sv){ s, (uint32_t)(p - s) };
	*pp = p;
	return 0;
}

/* .section name [, "flags" [, @type [, entsize]]] ; push = .pushsection */
static int d_section(As *a, const char *p, const char *e, int push)
{
	Sv name;
	if (parse_name(a, &p, e, &name)) return -1;
	uint32_t type = 0; uint64_t flags = 0; uint32_t entsize = 0;
	int have_flags = 0;
	int64_t sub = 0;
	p = ws(p, e);
	if (p < e && *p == ',') {
		p = ws(p + 1, e);
		if (push && p < e && *p != '"') {	/* .pushsection name, subsection */
			if (abs_expr(a, &p, e, &sub)) return -1;
			p = ws(p, e);
			if (p < e && *p == ',') p = ws(p + 1, e);
		}
		if (p < e && *p == '"') {
			have_flags = 1;
			for (p++; p < e && *p != '"'; p++) {
				switch (*p) {
				case 'a': flags |= SHF_ALLOC; break;
				case 'w': flags |= SHF_WRITE; break;
				case 'x': flags |= SHF_EXECINSTR; break;
				case 'M': flags |= SHF_MERGE; break;
				case 'S': flags |= SHF_STRINGS; break;
				case 'T': flags |= SHF_TLS; break;
				case 'o': flags |= SHF_LINK_ORDER; break;
				case 'e': flags |= SHF_EXCLUDE; break;
				case 'R': flags |= 0x200000; break;	/* SHF_GNU_RETAIN */
				case 'G': return err(a, "section groups are not supported");
				default: return err(a, "unknown flag '%c' in .section", *p);
				}
			}
			p++;
			p = ws(p, e);
			if (p < e && *p == ',') {
				p = ws(p + 1, e);
				if (p < e && (*p == '@' || *p == '%')) {
					p++;
					const char *s = p;
					while (p < e && isidc((unsigned char)*p)) p++;
					Sv t = { s, (uint32_t)(p - s) };
					if (sveq(t, "progbits")) type = SHT_PROGBITS;
					else if (sveq(t, "nobits")) type = SHT_NOBITS;
					else if (sveq(t, "note")) type = SHT_NOTE;
					else if (sveq(t, "init_array")) type = SHT_INIT_ARRAY;
					else if (sveq(t, "fini_array")) type = SHT_FINI_ARRAY;
					else if (sveq(t, "preinit_array")) type = SHT_PREINIT_ARRAY;
					else return err(a, "unknown section type");
					p = ws(p, e);
					if (p < e && *p == ',') {
						p++;
						int64_t es;
						if (abs_expr(a, &p, e, &es)) return -1;
						entsize = (uint32_t)es;
					}
				}
			}
		}
	}
	if ((flags & SHF_MERGE) && !entsize) return err(a, "entry size not specified");
	if (!type) {
		if (name.n >= 5 && !memcmp(name.s, ".note", 5)) type = SHT_NOTE;
		else if (hasprefix(name, ".init_array")) type = SHT_INIT_ARRAY;
		else if (hasprefix(name, ".bss") || hasprefix(name, ".tbss")) type = SHT_NOBITS;
		else if (hasprefix(name, ".fini_array")) type = SHT_FINI_ARRAY;
		else if (hasprefix(name, ".preinit_array")) type = SHT_PREINIT_ARRAY;
		else type = SHT_PROGBITS;
	}
	if (!have_flags) {
		if (hasprefix(name, ".rodata") || sveq(name, ".rodata1")) flags = SHF_ALLOC;
		else if (sveq(name, ".fini") || sveq(name, ".init") || hasprefix(name, ".text")) flags = SHF_ALLOC | SHF_EXECINSTR;
		else if (hasprefix(name, ".data") || sveq(name, ".data1") || hasprefix(name, ".bss") ||
			 hasprefix(name, ".init_array") || hasprefix(name, ".fini_array") || hasprefix(name, ".preinit_array"))
			flags = SHF_ALLOC | SHF_WRITE;
		else if (hasprefix(name, ".tdata") || hasprefix(name, ".tbss")) flags = SHF_ALLOC | SHF_WRITE | SHF_TLS;
	}
	if (push) {
		if (a->nss >= MAXSECSTK) return err(a, "section stack overflow");
		a->ss[a->nss].cur = a->cur; a->ss[a->nss].prev = a->prev; a->nss++;
	}
	uint32_t s = elf_section(a->o, name.s, name.n, type, flags, entsize, 0);
	switch_to(a, elf_subsection(a->o, s, (int32_t)sub));
	return 0;
}

/* ---------------------------------------------------------------- data */

static int emit_value(As *a, int size, Val *v)
{
	ElfObj *o = a->o;
	elf_data(o, a->cur);
	uint64_t off = elf_frag(o, a->cur)->size;
	uint8_t *p = elf_grow(o, a->cur, (size_t)size);
	fold(a, v);
	if (!v->add && !v->sub) {
		int64_t x = v->c;
		if (size < 8) {
			int64_t lo = -((int64_t)1 << (8 * size - 1)), hi = ((int64_t)1 << (8 * size)) - 1;
			if (x < lo || x > hi) return err(a, "out of range literal value");
		}
		for (int i = 0; i < size; i++) p[i] = (uint8_t)((uint64_t)x >> 8 * i);
		return 0;
	}
	memset(p, 0, (size_t)size);
	uint32_t t = size == 8 ? R_AARCH64_ABS64 : size == 4 ? R_AARCH64_ABS32 : size == 2 ? R_AARCH64_ABS16 : R_VCC_ABS8;
	elf_fixup(o, a->cur, off, p, t, *v);
	return 0;
}

static int d_data(As *a, const char *p, const char *e, int size)
{
	p = ws(p, e);
	if (p >= e) return 0;
	for (;;) {
		Val v;
		if (val_expr(a, &p, e, &v)) return -1;
		if (emit_value(a, size, &v)) return -1;
		p = ws(p, e);
		if (p >= e) return 0;
		if (*p != ',') return err(a, "unexpected token in directive");
		p++;
	}
}

static int parse_string(As *a, const char **pp, const char *e, char *out, size_t *n)
{
	const char *p = ws(*pp, e);
	if (p >= e || *p != '"') return err(a, "expected string");
	p++;
	size_t k = 0;
	while (p < e && *p != '"') {
		char c = *p++;
		if (c == '\\' && p < e) {
			c = *p++;
			switch (c) {
			case 'n': c = '\n'; break; case 't': c = '\t'; break; case 'r': c = '\r'; break;
			case 'b': c = '\b'; break; case 'f': c = '\f'; break; case '"': break; case '\\': break;
			case 'x': { int v = 0; while (p < e && isxdigit((unsigned char)*p)) { v = v * 16 + (isdigit((unsigned char)*p) ? *p - '0' : tolower((unsigned char)*p) - 'a' + 10); p++; } c = (char)v; break; }
			default:
				if (c >= '0' && c <= '7') {
					int v = c - '0';
					for (int i = 0; i < 2 && p < e && *p >= '0' && *p <= '7'; i++) v = v * 8 + (*p++ - '0');
					c = (char)v;
				}
			}
		}
		out[k++] = c;
	}
	if (p >= e) return err(a, "unterminated string");
	*pp = p + 1;
	*n = k;
	return 0;
}

static int d_ascii(As *a, const char *p, const char *e, int z)
{
	p = ws(p, e);
	if (p >= e) return 0;
	for (;;) {
		char buf[4096];
		size_t n;
		if ((size_t)(e - p) > sizeof buf) return err(a, "string too long");
		if (parse_string(a, &p, e, buf, &n)) return -1;
		elf_data(a->o, a->cur);
		elf_bytes(a->o, a->cur, buf, n);
		if (z) elf_fill(a->o, a->cur, 1, 0);
		p = ws(p, e);
		if (p >= e) return 0;
		if (*p == ',') p++;
		else if (*p != '"') return err(a, "unexpected token in directive");
	}
}

static int code_section(As *a) { return (elf_sec(a->o, elf_frag(a->o, a->cur)->sec)->flags & SHF_EXECINSTR) != 0; }

/* .align/.p2align (p2 = 1) or .balign (p2 = 0) */
static int d_align(As *a, const char *p, const char *e, int p2)
{
	int64_t n, fill = 0, max = 0;
	int hasfill = 0;
	if (abs_expr(a, &p, e, &n)) return -1;
	p = ws(p, e);
	if (p < e && *p == ',') {
		p = ws(p + 1, e);
		if (p < e && *p != ',') { if (abs_expr(a, &p, e, &fill)) return -1; hasfill = 1; }
		p = ws(p, e);
		if (p < e && *p == ',') { p++; if (abs_expr(a, &p, e, &max)) return -1; }
	}
	unsigned l2;
	if (p2) {
		if (n < 0 || n >= 32) return err(a, "invalid alignment value");
		l2 = (unsigned)n;
	} else {
		if (n <= 0) n = 1;
		if (n & (n - 1)) return err(a, "alignment must be a power of 2");
		l2 = (unsigned)__builtin_ctzll((uint64_t)n);
	}
	elf_align(a->o, a->cur, l2, !hasfill && code_section(a), (uint8_t)fill, (uint64_t)max);
	return 0;
}

static int d_space(As *a, const char *p, const char *e)
{
	int64_t n, fill = 0;
	if (abs_expr(a, &p, e, &n)) return -1;
	p = ws(p, e);
	if (p < e && *p == ',') { p++; if (abs_expr(a, &p, e, &fill)) return -1; }
	if (n < 0) return err(a, "invalid number of bytes");
	elf_data(a->o, a->cur);
	elf_fill(a->o, a->cur, (uint64_t)n, (uint8_t)fill);
	return 0;
}

static int d_fill(As *a, const char *p, const char *e)
{
	int64_t n, size = 1, val = 0;
	if (abs_expr(a, &p, e, &n)) return -1;
	p = ws(p, e);
	if (p < e && *p == ',') {
		p++; if (abs_expr(a, &p, e, &size)) return -1;
		p = ws(p, e);
		if (p < e && *p == ',') { p++; if (abs_expr(a, &p, e, &val)) return -1; }
	}
	if (size > 8) size = 8;
	elf_data(a->o, a->cur);
	for (int64_t i = 0; i < n; i++) {
		uint8_t *q = elf_grow(a->o, a->cur, (size_t)size);
		for (int k = 0; k < size; k++) q[k] = (uint8_t)((uint64_t)val >> 8 * k);
	}
	return 0;
}

static int d_org(As *a, const char *p, const char *e)
{
	Val v;
	int64_t fill = 0;
	if (val_expr(a, &p, e, &v)) return -1;
	p = ws(p, e);
	if (p < e && *p == ',') { p++; if (abs_expr(a, &p, e, &fill)) return -1; }
	ElfFrag *f = elf_frag(a->o, a->cur);
	int64_t target;
	if (v.add && !v.sub) {
		ElfSym *y = elf_symp(a->o, v.add);
		if (!(y->flags & ESYM_DEFINED) || y->frag != a->cur) return err(a, "unsupported .org target");
		target = (int64_t)y->value + v.c;
	} else if (!v.add && !v.sub) {
		if (f->sub) return err(a, "unsupported absolute .org in a subsection");
		target = v.c;
	} else return err(a, "expected assembly-time absolute expression");
	if (target < (int64_t)f->size) return err(a, "attempt to move .org backwards");
	elf_fill(a->o, a->cur, (uint64_t)target - f->size, (uint8_t)fill);
	return 0;
}

static int d_incbin(As *a, const char *p, const char *e)
{
	char path[1024]; size_t n;
	if ((size_t)(e - p) >= sizeof path) return err(a, "path too long");
	if (parse_string(a, &p, e, path, &n)) return -1;
	path[n] = 0;
	int64_t skip = 0, count = -1;
	p = ws(p, e);
	if (p < e && *p == ',') {
		p++; if (abs_expr(a, &p, e, &skip)) return -1;
		p = ws(p, e);
		if (p < e && *p == ',') { p++; if (abs_expr(a, &p, e, &count)) return -1; }
	}
	int fd = open(path, O_RDONLY);
	if (fd < 0) return err(a, "Could not find incbin file '%s'", path);
	struct stat st;
	fstat(fd, &st);
	size_t sz = (size_t)st.st_size;
	if ((uint64_t)skip > sz) skip = (int64_t)sz;
	size_t len = sz - (size_t)skip;
	if (count >= 0 && (size_t)count < len) len = (size_t)count;
	elf_data(a->o, a->cur);
	if (len) {
		uint8_t *dst = elf_grow(a->o, a->cur, len);
		size_t got = 0;
		while (got < len) {
			ssize_t k = pread(fd, dst + got, len - got, skip + (off_t)got);
			if (k <= 0) break;
			got += (size_t)k;
		}
	}
	close(fd);
	return 0;
}

static int d_inst(As *a, const char *p, const char *e)
{
	for (;;) {
		Val v;
		if (val_expr(a, &p, e, &v)) return -1;
		fold(a, &v);
		if (!is_abs(a, &v)) return err(a, "expected constant expression in .inst");
		elf_insn(a->o, a->cur, (uint32_t)v.c);
		p = ws(p, e);
		if (p >= e) return 0;
		if (*p != ',') return err(a, "unexpected token in .inst");
		p++;
	}
}

/* ---------------------------------------------------------------- symbols directives */

static int d_symattr(As *a, const char *p, const char *e, int what)
{
	for (;;) {
		Sv n;
		if (parse_name(a, &p, e, &n)) return -1;
		uint32_t y = sym(a, n.s, n.n);
		if (what != 'e') elf_reg(a->o, y);
		ElfSym *s = elf_symp(a->o, y);
		switch (what) {
		case 'g': elf_bind(a->o, y, STB_GLOBAL); break;
		case 'w': elf_bind(a->o, y, STB_WEAK); break;
		case 'l': elf_bind(a->o, y, STB_LOCAL); break;
		case 'h': elf_vis(a->o, y, STV_HIDDEN); if (!(s->flags & ESYM_BINDSET) && 0) {} break;
		case 'p': elf_vis(a->o, y, STV_PROTECTED); break;
		case 'i': elf_vis(a->o, y, STV_INTERNAL); break;
		case 'e': break;
		}
		if (what != 'e' && what != 'l') s->flags &= ~ESYM_TEMP;	/* .globl .Lx makes it real */
		p = ws(p, e);
		if (p >= e) return 0;
		if (*p != ',') return err(a, "unexpected token in directive");
		p++;
	}
}

static int d_type(As *a, const char *p, const char *e)
{
	Sv n;
	if (parse_name(a, &p, e, &n)) return -1;
	uint32_t y = sym(a, n.s, n.n);
	p = ws(p, e);
	if (p < e && *p == ',') p = ws(p + 1, e);
	if (p < e && (*p == '%' || *p == '@')) p++;
	const char *s = p;
	while (p < e && isidc((unsigned char)*p)) p++;
	Sv t = { s, (uint32_t)(p - s) };
	int ty;
	if (sveq(t, "function") || sveq(t, "STT_FUNC")) ty = STT_FUNC;
	else if (sveq(t, "object") || sveq(t, "STT_OBJECT")) ty = STT_OBJECT;
	else if (sveq(t, "notype") || sveq(t, "STT_NOTYPE")) ty = STT_NOTYPE;
	else if (sveq(t, "tls_object") || sveq(t, "STT_TLS")) ty = STT_TLS;
	else if (sveq(t, "gnu_indirect_function")) ty = STT_GNU_IFUNC;
	else return err(a, "unsupported attribute in '.type' directive");
	elf_reg(a->o, y);
	elf_type(a->o, y, ty);
	return 0;
}

static int d_size(As *a, const char *p, const char *e)
{
	Sv n;
	if (parse_name(a, &p, e, &n)) return -1;
	uint32_t y = sym(a, n.s, n.n);
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected comma in '.size' directive");
	p++;
	Val v;
	if (val_expr(a, &p, e, &v)) return -1;
	fold(a, &v);
	if (is_abs(a, &v)) { elf_symp(a->o, y)->size = (uint64_t)v.c; elf_symp(a->o, y)->flags &= ~ESYM_SIZE_EXPR; }
	else elf_size_expr(a->o, y, v);
	return 0;
}

/* name = expr / .set name, expr */
static int assign(As *a, Sv n, const char *p, const char *e)
{
	uint32_t y = sym(a, n.s, n.n);
	Val v;
	if (val_expr(a, &p, e, &v)) return -1;
	elf_reg(a->o, y);
	elf_reg(a->o, v.add); elf_reg(a->o, v.sub);
	fold(a, &v);
	ElfSym *s = elf_symp(a->o, y);
	if ((s->flags & ESYM_DEFINED) && !(s->flags & ESYM_ABS) && !(s->flags & ESYM_NOSYMTAB))
		return err(a, "redefinition of '%.*s'", (int)n.n, n.s);
	Var *vr = a->vcount ? var_slot(a, y, 0) : 0;
	if (vr) vr->v.add = (uint32_t)-1;	/* dead */
	if (!v.add && !v.sub) {
		s->flags &= ~ESYM_DEFINED;
		elf_define_abs(a->o, y, (uint64_t)v.c);
		return 0;
	}
	if (v.add && !v.sub) {
		ElfSym *t = elf_symp(a->o, v.add);
		if ((t->flags & ESYM_DEFINED) && !(t->flags & ESYM_SECTION)) {
			s->flags &= ~(ESYM_DEFINED | ESYM_ABS);
			elf_define(a->o, y, t->frag, t->value + (uint64_t)v.c);
			s->alias = t->alias ? t->alias : v.add;
			return 0;
		}
	}
	/* a variable: references see the expression, aliases resolve at the end */
	s->flags &= ~(ESYM_DEFINED | ESYM_ABS);
	vr = var_slot(a, y, 1);
	vr->v = v;
	return 0;
}

/* ---------------------------------------------------------------- macros */

static Macro *find_macro(As *a, const char *s, size_t n)
{
	uint32_t h = hsv(s, n) & a->mmask;
	for (Macro *m = a->mtab[h]; m; m = m->next)
		if (m->name.n == n && !memcmp(m->name.s, s, n)) return m;
	return 0;
}

/* first word of a statement */
static Sv first_word(const char *p, const char *e)
{
	p = ws(p, e);
	const char *s = p;
	while (p < e && isidc((unsigned char)*p)) p++;
	return (Sv){ s, (uint32_t)(p - s) };
}

static int is_loop_dir(Sv w)
{
	return sveq(w, ".rept") || sveq(w, ".irp") || sveq(w, ".irpc") || sveq(w, ".rep");
}

/* Collect a macro-like body from the current frame up to the matching
 * .endm (macro = 1) or .endr.  Returns the raw text. */
static int collect_body(As *a, int macro, Sv *body)
{
	Frame *f = &a->fr[a->nfr - 1];
	const char *start = f->p;
	int depth = 0;
	while (f->p < f->e) {
		const char *raw;
		size_t n = next_stmt(a, f, &raw);
		const char *s = (const char *)a->sb.base;
		Sv w = first_word(s, s + n);
		if (macro) {
			if (sveq(w, ".endm") || sveq(w, ".endmacro")) {
				if (!depth) { *body = (Sv){ start, (uint32_t)(raw - start) }; return 0; }
				depth--;
			} else if (sveq(w, ".macro")) depth++;
		} else {
			if (sveq(w, ".endr")) {
				if (!depth) { *body = (Sv){ start, (uint32_t)(raw - start) }; return 0; }
				depth--;
			} else if (is_loop_dir(w)) depth++;
		}
	}
	return err(a, macro ? "no matching '.endmacro' in definition" : "no matching '.endr' in definition");
}

/* parseMacroArgument: one argument as MC tokenises it, written as the text
 * the expansion will insert (strings unquoted unless vararg). */
static int is_op_start(const char *p, const char *e)
{
	char c = *p;
	switch (c) {
	case '+': case '-': case '~': case '/': case '*': case '=': case '|': case '^':
	case '&': case '!': case '<': case '>':
		return 1;
	case '.':
		return !(p + 1 < e && isidc((unsigned char)p[1]));
	}
	return 0;
}
static int op_len(const char *p, const char *e)
{
	char c = p[0], d = p + 1 < e ? p[1] : 0;
	if ((c == '=' && d == '=') || (c == '|' && d == '|') || (c == '&' && d == '&') || (c == '!' && d == '=') ||
	    (c == '<' && (d == '=' || d == '<' || d == '>')) || (c == '>' && (d == '=' || d == '>')))
		return 2;
	return 1;
}

static int macro_arg(As *a, const char **pp, const char *e, int vararg, Sv *out)
{
	const char *p = *pp;
	char *buf = (char *)rget(&a->xs, (size_t)(e - p) + 1);
	size_t n = 0;
	if (vararg) {
		p = ws(p, e);
		const char *q = e;
		while (q > p && (q[-1] == ' ' || q[-1] == '\t')) q--;
		memcpy(buf, p, (size_t)(q - p)); n = (size_t)(q - p);
		*pp = e;
		*out = (Sv){ buf, (uint32_t)n };
		return 0;
	}
	int depth = 0;
	for (;;) {
		int space = 0;
		if (depth == 0) {
			if (p < e && *p == ',') break;
			if (p < e && (*p == ' ' || *p == '\t')) { p = ws(p, e); space = 1; }
			if (p < e && is_op_start(p, e)) {
				if (*p == '=' && p + 1 < e && p[1] != '=') return err(a, "unexpected token in macro instantiation");
				int k = op_len(p, e);
				memcpy(buf + n, p, (size_t)k); n += (size_t)k; p += k;
				p = ws(p, e);
				continue;
			}
			if (space) break;
		}
		if (p >= e) break;
		char c = *p;
		if (c == '(') depth++;
		else if (c == ')' && depth) depth--;
		if (c == '"') {
			const char *s = ++p;
			while (p < e && *p != '"') { if (*p == '\\' && p + 1 < e) p++; p++; }
			memcpy(buf + n, s, (size_t)(p - s)); n += (size_t)(p - s);
			if (p < e) p++;
			continue;
		}
		if (c == ' ' || c == '\t') {
			const char *s = p; p = ws(p, e);
			memcpy(buf + n, s, (size_t)(p - s)); n += (size_t)(p - s);
			continue;
		}
		if (isidc((unsigned char)c)) {
			const char *s = p;
			while (p < e && isidc((unsigned char)*p)) p++;
			memcpy(buf + n, s, (size_t)(p - s)); n += (size_t)(p - s);
			continue;
		}
		if (c == '=' && depth == 0) return err(a, "unexpected token in macro instantiation");
		buf[n++] = c; p++;
	}
	if (depth) return err(a, "unbalanced parentheses in argument");
	a->xs.used = (size_t)((uint8_t *)buf - a->xs.base) + n;
	*pp = p;
	*out = (Sv){ buf, (uint32_t)n };
	return 0;
}

static void xput(As *a, char **o, const char *s, size_t n)
{
	(void)a;
	memcpy(*o, s, n); *o += n;
}

/* expandMacro: body with \param, \@, \+, \() substitutions */
static size_t expand(As *a, char *o, Sv body, const Param *ps, const Sv *args, uint32_t np, int at, uint32_t count)
{
	char *o0 = o;
	const char *b = body.s, *e = body.s + body.n;
	size_t i = 0, n = body.n;
	while (i < n) {
		if (b[i] == '\\' && i + 1 != n) {
			if (at && b[i + 1] == '@') { o += sprintf(o, "%u", a->ninst); i += 2; continue; }
			if (b[i + 1] == '+') { o += sprintf(o, "%u", count); i += 2; continue; }
			if (b[i + 1] == '(' && i + 2 < n && b[i + 2] == ')') { i += 3; continue; }
			size_t pos = ++i;
			while (i < n && isidc((unsigned char)b[i])) i++;
			Sv arg = { b + pos, (uint32_t)(i - pos) };
			uint32_t k;
			for (k = 0; k < np; k++) if (ps[k].name.n == arg.n && !memcmp(ps[k].name.s, arg.s, arg.n)) break;
			if (k == np) { *o++ = '\\'; xput(a, &o, arg.s, arg.n); }
			else xput(a, &o, args[k].s, args[k].n);
			continue;
		}
		*o++ = b[i++];
	}
	(void)e;
	return (size_t)(o - o0);
}

static size_t expand_bound(Sv body, const Sv *args, uint32_t np)
{
	size_t m = 0;
	for (uint32_t k = 0; k < np; k++) if (args[k].n > m) m = args[k].n;
	return body.n * (m + 12) + 16;
}

static int d_macro(As *a, const char *p, const char *e)
{
	Sv name;
	p = ws(p, e);
	const char *s = p;
	while (p < e && isidc((unsigned char)*p)) p++;
	if (p == s) return err(a, "expected identifier in '.macro' directive");
	name = svdup(a, s, (size_t)(p - s));
	Param ps[64]; uint32_t np = 0;
	p = ws(p, e);
	if (p < e && *p == ',') p = ws(p + 1, e);
	while (p < e) {
		if (np >= 64) return err(a, "too many macro parameters");
		Param *q = &ps[np++];
		memset(q, 0, sizeof *q);
		s = p;
		while (p < e && isidc((unsigned char)*p)) p++;
		if (p == s) return err(a, "expected identifier in '.macro' directive");
		q->name = svdup(a, s, (size_t)(p - s));
		p = ws(p, e);
		if (p < e && *p == ':') {
			p = ws(p + 1, e);
			s = p;
			while (p < e && isidc((unsigned char)*p)) p++;
			Sv qq = { s, (uint32_t)(p - s) };
			if (sveq(qq, "req")) q->req = 1;
			else if (sveq(qq, "vararg")) q->vararg = 1;
			else return err(a, "invalid macro parameter qualifier");
			p = ws(p, e);
		}
		if (p < e && *p == '=') {
			p = ws(p + 1, e);
			Sv d;
			size_t mark = a->xs.used;
			if (macro_arg(a, &p, e, 0, &d)) return -1;
			q->def = svdup(a, d.s, d.n);
			a->xs.used = mark;
			p = ws(p, e);
		}
		if (p < e && *p == ',') p = ws(p + 1, e);
	}
	Sv body;
	if (collect_body(a, 1, &body)) return -1;
	Macro *m = rget(&a->defs, sizeof *m);
	m->name = name;
	m->params = rget(&a->defs, sizeof(Param) * (np ? np : 1));
	memcpy(m->params, ps, sizeof(Param) * np);
	m->np = np;
	m->body = svdup(a, body.s, body.n);
	m->count = 0;
	if (a->mcount * 2 >= a->mmask) {
		uint32_t nm = a->mmask * 2 + 1;
		Macro **t = rget(&a->defs, sizeof *t * (nm + 1));
		memset(t, 0, sizeof *t * (nm + 1));
		for (uint32_t i = 0; i <= a->mmask; i++)
			for (Macro *x = a->mtab[i], *nx; x; x = nx) { nx = x->next; uint32_t h = hsv(x->name.s, x->name.n) & nm; x->next = t[h]; t[h] = x; }
		a->mtab = t; a->mmask = nm;
	}
	if (find_macro(a, name.s, name.n)) return err(a, "macro '%.*s' is already defined", (int)name.n, name.s);
	uint32_t h = hsv(name.s, name.n) & a->mmask;
	m->next = a->mtab[h]; a->mtab[h] = m; a->mcount++;
	return 0;
}

static int d_purgem(As *a, const char *p, const char *e)
{
	Sv n = first_word(p, e);
	uint32_t h = hsv(n.s, n.n) & a->mmask;
	for (Macro **pm = &a->mtab[h]; *pm; pm = &(*pm)->next)
		if ((*pm)->name.n == n.n && !memcmp((*pm)->name.s, n.s, n.n)) { *pm = (*pm)->next; a->mcount--; return 0; }
	return err(a, "macro '%.*s' is not defined", (int)n.n, n.s);
}

static int invoke(As *a, Macro *m, const char *p, const char *e)
{
	size_t mark = a->xs.used;
	Sv args[64];
	uint8_t set[64] = { 0 };
	uint32_t np = m->np, pi = 0;
	p = ws(p, e);
	int hasvararg = np && m->params[np - 1].vararg;
	for (uint32_t k = 0; k < np; k++) args[k] = (Sv){ "", 0 };
	if (p < e) {
		for (;;) {
			/* keyword argument? */
			const char *q = p;
			while (q < e && isidc((unsigned char)*q)) q++;
			const char *r = ws(q, e);
			int kw = q > p && r < e && *r == '=' && !(r + 1 < e && r[1] == '=');
			uint32_t k;
			if (kw) {
				for (k = 0; k < np; k++) if (m->params[k].name.n == (uint32_t)(q - p) && !memcmp(m->params[k].name.s, p, (size_t)(q - p))) break;
				if (k == np) return err(a, "parameter named '%.*s' does not exist for macro '%.*s'", (int)(q - p), p, (int)m->name.n, m->name.s);
				p = ws(r + 1, e);
			} else {
				if (pi >= np) {
					if (!np) return err(a, "too many positional arguments");
					return err(a, "too many positional arguments");
				}
				k = pi++;
			}
			if (macro_arg(a, &p, e, hasvararg && k == np - 1, &args[k])) return -1;
			set[k] = 1;
			p = ws(p, e);
			if (p >= e) break;
			if (*p == ',') { p = ws(p + 1, e); if (p >= e) { if (pi < np) set[pi] = 1, args[pi] = (Sv){ "", 0 }; break; } }
		}
	}
	for (uint32_t k = 0; k < np; k++) {
		if (set[k] && args[k].n) continue;
		if (!set[k] || !args[k].n) {
			if (m->params[k].def.n && !(set[k] && args[k].n)) args[k] = m->params[k].def;
			else if (m->params[k].req && !set[k]) return err(a, "missing value for required parameter '%.*s' in macro '%.*s'", (int)m->params[k].name.n, m->params[k].name.s, (int)m->name.n, m->name.s);
			else if (m->params[k].req && !args[k].n) return err(a, "missing value for required parameter '%.*s' in macro '%.*s'", (int)m->params[k].name.n, m->params[k].name.s, (int)m->name.n, m->name.s);
		}
	}
	char *o = (char *)rget(&a->xs, expand_bound(m->body, args, np));
	size_t n = expand(a, o, m->body, m->params, args, np, 1, m->count);
	a->xs.used = (size_t)((uint8_t *)o - a->xs.base) + n;
	a->ninst++;
	m->count++;
	return push_frame(a, o, n, FR_MACRO, mark);
}

static int d_rept(As *a, const char *p, const char *e)
{
	int64_t cnt;
	if (abs_expr(a, &p, e, &cnt)) return -1;
	if (cnt < 0) return err(a, "Count is negative");
	Sv body;
	if (collect_body(a, 0, &body)) return -1;
	size_t mark = a->xs.used;
	char *o = (char *)rget(&a->xs, expand_bound(body, 0, 0) * (size_t)(cnt ? cnt : 1));
	size_t n = 0;
	for (int64_t i = 0; i < cnt; i++) n += expand(a, o + n, body, 0, 0, 0, 0, 0);
	a->xs.used = (size_t)((uint8_t *)o - a->xs.base) + n;
	return push_frame(a, o, n, FR_LOOP, mark);
}

static int d_irp(As *a, const char *p, const char *e, int chars)
{
	p = ws(p, e);
	const char *s = p;
	while (p < e && isidc((unsigned char)*p)) p++;
	if (p == s) return err(a, "expected identifier in '.irp' directive");
	Param par; memset(&par, 0, sizeof par);
	par.name = (Sv){ s, (uint32_t)(p - s) };
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected comma");
	p = ws(p + 1, e);
	size_t mark = a->xs.used;
	Sv vals[1024]; uint32_t nv = 0;
	for (;;) {
		if (nv >= 1024) return err(a, "too many .irp values");
		if (macro_arg(a, &p, e, 0, &vals[nv++])) return -1;
		p = ws(p, e);
		if (p >= e) break;
		if (*p == ',') p = ws(p + 1, e);
	}
	Sv body;
	/* the param name lives in a->sb, which collect_body overwrites */
	par.name = svdup(a, par.name.s, par.name.n);
	if (collect_body(a, 0, &body)) return -1;
	if (chars) {
		Sv v = vals[0];
		nv = 0;
		Sv cv[1024];
		for (uint32_t i = 0; i < v.n && i < 1024; i++) cv[nv++] = (Sv){ v.s + i, 1 };
		memcpy(vals, cv, sizeof(Sv) * nv);
	}
	size_t tot = 0;
	for (uint32_t i = 0; i < nv; i++) tot += expand_bound(body, &vals[i], 1);
	char *o = (char *)rget(&a->xs, tot + 1);
	size_t n = 0;
	for (uint32_t i = 0; i < nv; i++) n += expand(a, o + n, body, &par, &vals[i], 1, 1, 0);
	a->xs.used = (size_t)((uint8_t *)o - a->xs.base) + n;
	return push_frame(a, o, n, FR_LOOP, mark);
}

static int d_exitm(As *a)
{
	for (int i = a->nfr - 1; i >= 0; i--) {
		if (a->fr[i].kind == FR_MACRO) {
			a->ncs = (int)a->fr[i].ncond;
			while (a->nfr > i) pop_frame(a);
			return 0;
		}
	}
	return err(a, "unexpected '.exitm' in file, no current macro definition");
}

/* ---------------------------------------------------------------- conditionals */

static int cond_ignore(As *a) { return a->ncs && a->cs[a->ncs - 1].ignore; }

static int push_cond(As *a, int met)
{
	if (a->ncs >= MAXCOND) return err(a, "conditionals nested too deeply");
	int parent = cond_ignore(a);
	Cond *c = &a->cs[a->ncs++];
	c->kind = 1; c->met = (uint8_t)(!parent && met); c->ignore = (uint8_t)(parent || !met);
	return 0;
}

static Sv trim(const char *p, const char *e)
{
	p = ws(p, e);
	while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
	return (Sv){ p, (uint32_t)(e - p) };
}

/* .ifc/.ifnc operand: quoted string or text up to ',' */
static Sv ifc_str(const char **pp, const char *e)
{
	const char *p = ws(*pp, e);
	if (p < e && *p == '\'') {	/* GNU allows quotes; MC: string token */
	}
	if (p < e && *p == '"') {
		const char *s = ++p;
		while (p < e && *p != '"') p++;
		Sv r = { s, (uint32_t)(p - s) };
		if (p < e) p++;
		*pp = p;
		return r;
	}
	const char *s = p;
	while (p < e && *p != ',') p++;
	*pp = p;
	return trim(s, p);
}

/* conditional directives; returns 1 if w was one */
static int cond_dir(As *a, Sv w, const char *p, const char *e, int *rc)
{
	if (w.n < 3 || w.s[0] != '.') return 0;
	*rc = 0;
	if (sveq(w, ".endif")) {
		if (!a->ncs) { *rc = err(a, "Encountered a .endif that doesn't follow an .if or .else"); return 1; }
		a->ncs--;
		return 1;
	}
	if (sveq(w, ".else")) {
		if (!a->ncs || a->cs[a->ncs - 1].kind == 3) { *rc = err(a, "Encountered a .else that doesn't follow an .if or an .elseif"); return 1; }
		Cond *c = &a->cs[a->ncs - 1];
		int parent = a->ncs > 1 && a->cs[a->ncs - 2].ignore;
		c->kind = 3;
		c->ignore = (uint8_t)(parent || c->met);
		return 1;
	}
	if (sveq(w, ".elseif")) {
		if (!a->ncs || a->cs[a->ncs - 1].kind == 3) { *rc = err(a, "Encountered a .elseif that doesn't follow an .if or .elseif"); return 1; }
		Cond *c = &a->cs[a->ncs - 1];
		int parent = a->ncs > 1 && a->cs[a->ncs - 2].ignore;
		c->kind = 2;
		if (parent || c->met) { c->ignore = 1; return 1; }
		int64_t v;
		if (abs_expr(a, &p, e, &v)) { *rc = -1; return 1; }
		c->met = v != 0; c->ignore = !c->met;
		return 1;
	}
	if (memcmp(w.s, ".if", 3)) return 0;
	Sv k = { w.s + 3, w.n - 3 };
	if (cond_ignore(a)) {	/* any .if* nests */
		if (k.n == 0 || sveq(k, "ne") || sveq(k, "eq") || sveq(k, "gt") || sveq(k, "ge") || sveq(k, "lt") ||
		    sveq(k, "le") || sveq(k, "def") || sveq(k, "ndef") || sveq(k, "notdef") || sveq(k, "b") ||
		    sveq(k, "nb") || sveq(k, "c") || sveq(k, "nc") || sveq(k, "eqs") || sveq(k, "nes")) {
			*rc = push_cond(a, 0);
			return 1;
		}
		return 0;
	}
	int met;
	if (k.n == 0 || sveq(k, "ne") || sveq(k, "eq") || sveq(k, "gt") || sveq(k, "ge") || sveq(k, "lt") || sveq(k, "le")) {
		int64_t v;
		if (abs_expr(a, &p, e, &v)) { *rc = -1; return 1; }
		if (k.n == 0 || sveq(k, "ne")) met = v != 0;
		else if (sveq(k, "eq")) met = v == 0;
		else if (sveq(k, "gt")) met = v > 0;
		else if (sveq(k, "ge")) met = v >= 0;
		else if (sveq(k, "lt")) met = v < 0;
		else met = v <= 0;
	} else if (sveq(k, "def") || sveq(k, "ndef") || sveq(k, "notdef")) {
		Sv n;
		if (parse_name(a, &p, e, &n)) { *rc = -1; return 1; }
		uint32_t y = elf_sym_lookup(a->o, n.s, n.n);
		int def = y && (elf_symp(a->o, y)->flags & ESYM_DEFINED);
		met = sveq(k, "def") ? def : !def;
	} else if (sveq(k, "b") || sveq(k, "nb")) {
		Sv t = trim(p, e);
		met = sveq(k, "b") ? t.n == 0 : t.n != 0;
	} else if (sveq(k, "c") || sveq(k, "nc")) {
		Sv x = ifc_str(&p, e);
		p = ws(p, e);
		if (p >= e || *p != ',') { *rc = err(a, "expected comma after first string for '.ifc' directive"); return 1; }
		p++;
		Sv y = ifc_str(&p, e);
		int eq = x.n == y.n && !memcmp(x.s, y.s, x.n);
		met = sveq(k, "c") ? eq : !eq;
	} else if (sveq(k, "eqs") || sveq(k, "nes")) {
		Sv x = ifc_str(&p, e);
		p = ws(p, e);
		if (p < e && *p == ',') p++;
		Sv y = ifc_str(&p, e);
		int eq = x.n == y.n && !memcmp(x.s, y.s, x.n);
		met = sveq(k, "eqs") ? eq : !eq;
	} else return 0;
	*rc = push_cond(a, met);
	return 1;
}

/* ---------------------------------------------------------------- instructions */

static int reg_alias(As *a, const char *s, size_t n, Sv *out)
{
	for (uint32_t i = a->nal; i-- > 0;)
		if (a->al[i].name.n == n && !strncasecmp(a->al[i].name.s, s, n)) { *out = a->al[i].reg; return 1; }
	return 0;
}

static int parse_regnum(const char *s, size_t n, int max)
{
	if (!n || n > 2) return -1;
	int v = 0;
	for (size_t i = 0; i < n; i++) { if (!isdigit((unsigned char)s[i])) return -1; v = v * 10 + s[i] - '0'; }
	if (n == 2 && s[0] == '0') return -1;
	return v <= max ? v : -1;
}

static int arrangement(const char *s, size_t n)
{
	char b[8];
	if (n >= sizeof b) return -1;
	for (size_t i = 0; i < n; i++) b[i] = (char)tolower((unsigned char)s[i]);
	b[n] = 0;
	static const char *t[] = { "", "8b", "16b", "4h", "8h", "2s", "4s", "1d", "2d", "1q", "b", "h", "s", "d", "q" };
	for (int i = 1; i < 15; i++) if (!strcmp(b, t[i])) return i;
	return -1;
}

/* Try to parse a register at p (identifier [s, s+n)); fill op. */
static int parse_reg(As *a, const char *s, size_t n, A64Op *op, int depth)
{
	Sv al;
	const char *dot = memchr(s, '.', n);
	size_t bn = dot ? (size_t)(dot - s) : n;
	if (depth < 4 && reg_alias(a, s, bn, &al)) {
		char buf[64];
		if (al.n + (n - bn) >= sizeof buf) return 0;
		memcpy(buf, al.s, al.n); memcpy(buf + al.n, s + bn, n - bn);
		return parse_reg(a, buf, al.n + (n - bn), op, depth + 1);
	}
	char c = (char)tolower((unsigned char)s[0]);
	memset(op, 0, sizeof *op);
	if (!dot) {
		if (sveqi(s, n, "sp")) { op->kind = A64O_REG; op->cls = A64_X; op->reg = 31; op->sp = 1; return 1; }
		if (sveqi(s, n, "wsp")) { op->kind = A64O_REG; op->cls = A64_W; op->reg = 31; op->sp = 1; return 1; }
		if (sveqi(s, n, "xzr")) { op->kind = A64O_REG; op->cls = A64_X; op->reg = 31; return 1; }
		if (sveqi(s, n, "wzr")) { op->kind = A64O_REG; op->cls = A64_W; op->reg = 31; return 1; }
		if (sveqi(s, n, "fp")) { op->kind = A64O_REG; op->cls = A64_X; op->reg = 29; return 1; }
		if (sveqi(s, n, "lr")) { op->kind = A64O_REG; op->cls = A64_X; op->reg = 30; return 1; }
		int r;
		if ((c == 'x' || c == 'w') && (r = parse_regnum(s + 1, n - 1, 30)) >= 0) {
			op->kind = A64O_REG; op->cls = c == 'x' ? A64_X : A64_W; op->reg = (uint8_t)r; return 1;
		}
		static const char fp[] = "qdshb";
		const char *k = strchr(fp, c);
		if (k && c && (r = parse_regnum(s + 1, n - 1, 31)) >= 0) {
			static const uint8_t cl[] = { A64_Q, A64_D, A64_S, A64_H, A64_B };
			op->kind = A64O_REG; op->cls = cl[k - fp]; op->reg = (uint8_t)r; return 1;
		}
		if (c == 'v' && (r = parse_regnum(s + 1, n - 1, 31)) >= 0) {
			op->kind = A64O_VREG; op->cls = A64_V; op->reg = (uint8_t)r; op->arr = A64_ARR_NONE; return 1;
		}
		return 0;
	}
	if (c != 'v') return 0;
	int r = parse_regnum(s + 1, bn - 1, 31);
	if (r < 0) return 0;
	int ar = arrangement(dot + 1, n - bn - 1);
	if (ar < 0) return 0;
	op->kind = A64O_VREG; op->cls = A64_V; op->reg = (uint8_t)r; op->arr = (uint8_t)ar;
	return 1;
}

static int shift_kind(const char *s, size_t n)
{
	static const char *t[] = { "lsl", "lsr", "asr", "ror", "msl", "uxtb", "uxth", "uxtw", "uxtx", "sxtb", "sxth", "sxtw", "sxtx" };
	for (int i = 0; i < 13; i++) if (sveqi(s, n, t[i])) return i;
	return -1;
}

static int mod_kind(const char *s, size_t n)
{
	static const struct { const char *n; int k; } t[] = {
		{ "lo12", A64_MOD_LO12 }, { "pg_hi21", A64_MOD_PG_HI21 }, { "pg_hi21_nc", A64_MOD_PG_HI21_NC },
		{ "abs_g0", A64_MOD_ABS_G0 }, { "abs_g0_nc", A64_MOD_ABS_G0_NC }, { "abs_g1", A64_MOD_ABS_G1 },
		{ "abs_g1_nc", A64_MOD_ABS_G1_NC }, { "abs_g2", A64_MOD_ABS_G2 }, { "abs_g2_nc", A64_MOD_ABS_G2_NC },
		{ "abs_g3", A64_MOD_ABS_G3 }, { "got", A64_MOD_GOT }, { "got_lo12", A64_MOD_GOT_LO12 },
		{ "abs_g0_s", A64_MOD_ABS_G0_S }, { "abs_g1_s", A64_MOD_ABS_G1_S }, { "abs_g2_s", A64_MOD_ABS_G2_S },
	};
	for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) if (sveqi(s, n, t[i].n)) return t[i].k;
	return -1;
}

/* immediate / expression operand, optionally with :mod: */
static int parse_imm(As *a, const char **pp, const char *e, A64Op *op)
{
	const char *p = ws(*pp, e);
	op->kind = A64O_IMM;
	if (p < e && *p == '#') { op->hash = 1; p = ws(p + 1, e); }
	if (p < e && *p == ':') {
		const char *s = ++p;
		while (p < e && *p != ':') p++;
		int m = mod_kind(s, (size_t)(p - s));
		if (m < 0 || p >= e) return err(a, "invalid relocation specifier");
		op->mod = (uint8_t)m;
		p++;
	}
	/* floating-point immediate */
	{
		const char *q = p;
		if (q < e && (*q == '-' || *q == '+')) q++;
		const char *d = q;
		while (q < e && isdigit((unsigned char)*q)) q++;
		if (q > d && q < e && (*q == '.' || ((*q == 'e' || *q == 'E') && !(d[0] == '0' && q - d == 1)))) {
			if (*q == '.' || *q == 'e' || *q == 'E') {
				char tmp[64]; size_t k = (size_t)(e - p) < 63 ? (size_t)(e - p) : 63;
				memcpy(tmp, p, k); tmp[k] = 0;
				char *end;
				op->fimm = strtod(tmp, &end);
				op->kind = A64O_FIMM;
				*pp = p + (end - tmp);
				return 0;
			}
		}
	}
	Val v;
	if (val_expr(a, &p, e, &v)) return -1;
	fold(a, &v);
	op->v = v;
	op->imm = v.c;
	op->hassym = v.add || v.sub;
	*pp = p;
	return 0;
}

static int parse_mem(As *a, const char **pp, const char *e, A64Op *op)
{
	const char *p = ws(*pp + 1, e);
	const char *s = p;
	while (p < e && isidc((unsigned char)*p)) p++;
	A64Op r;
	if (!parse_reg(a, s, (size_t)(p - s), &r, 0) || r.kind != A64O_REG || r.cls != A64_X)
		return err(a, "invalid operand for instruction");
	memset(op, 0, sizeof *op);
	op->kind = A64O_MEM; op->reg = r.reg; op->mode = A64M_OFF;
	p = ws(p, e);
	if (p < e && *p == ',') {
		p = ws(p + 1, e);
		s = p;
		while (p < e && isidc((unsigned char)*p)) p++;
		if (p > s && parse_reg(a, s, (size_t)(p - s), &r, 0) && r.kind == A64O_REG) {
			op->mode = A64M_REG; op->index = r.reg; op->idxcls = r.cls; op->shift = A64_LSL;
			p = ws(p, e);
			if (p < e && *p == ',') {
				p = ws(p + 1, e);
				s = p;
				while (p < e && isidc((unsigned char)*p)) p++;
				int k = shift_kind(s, (size_t)(p - s));
				if (k < 0) return err(a, "expected extend or shift");
				op->shift = (uint8_t)k;
				op->hasamt = 2;		/* extend given */
				p = ws(p, e);
				if (p < e && *p == '#') {
					int64_t amt;
					p++;
					if (abs_expr(a, &p, e, &amt)) return -1;
					op->imm = amt; op->hasamt = 3;
				}
			}
		} else {
			p = s;
			A64Op im; memset(&im, 0, sizeof im);
			if (parse_imm(a, &p, e, &im)) return -1;
			op->imm = im.imm; op->v = im.v; op->hassym = im.hassym; op->mod = im.mod; op->hasamt = 1;
		}
	}
	p = ws(p, e);
	if (p >= e || *p != ']') return err(a, "expected ']'");
	p = ws(p + 1, e);
	if (p < e && *p == '!') { op->mode = A64M_PRE; p++; }
	*pp = p;
	return 0;
}

static int names_ops(const char *m, size_t n)
{
	static const char *t[] = { "msr", "mrs", "sys", "sysl", "dmb", "dsb", "isb", "prfm", "prfum", "dc", "ic", "at",
				   "tlbi", "bti", "csel", "csinc", "csinv", "csneg", "cset", "csetm", "cinc", "cinv",
				   "cneg", "ccmp", "ccmn", "fcsel", "fccmp", "fccmpe", "hint", "psb", "tsb", "clrex", "sb", 0 };
	for (int i = 0; t[i]; i++) if (sveqi(m, n, t[i])) return 1;
	return 0;
}

typedef struct PoolS { uint32_t sec, label; Val v; uint32_t size; } Pool;

static uint32_t pool_entry(As *a, Val v, uint32_t size)
{
	uint32_t sec = elf_frag(a->o, a->cur)->sec;
	int plain = (v.add && !v.sub && !v.c) || (!v.add && !v.sub);
	if (plain)
		for (uint32_t i = 0; i < a->npool; i++) {
			Pool *q = &a->pool[i];
			if (q->sec == sec && q->size == size && q->v.add == v.add && q->v.sub == v.sub && q->v.c == v.c) return q->label;
		}
	if (a->npool == a->poolcap) {
		uint32_t nc = a->poolcap ? a->poolcap * 2 : 64;
		Pool *t = rget(&a->defs, sizeof *t * nc);
		if (a->npool) memcpy(t, a->pool, sizeof *t * a->npool);
		a->pool = t; a->poolcap = nc;
	}
	Pool *q = &a->pool[a->npool++];
	q->sec = sec; q->label = tmpsym(a); q->v = v; q->size = size;
	return q->label;
}

/* emitAll: per section in first-use order, align naturally, label, value */
static int flush_pools(As *a)
{
	for (uint32_t i = 0; i < a->npool; i++) {
		uint32_t sec = a->pool[i].sec;
		int seen = 0;
		for (uint32_t j = 0; j < i; j++) if (a->pool[j].sec == sec) seen = 1;
		if (seen) continue;
		a->cur = sec;
		for (uint32_t j = i; j < a->npool; j++) {
			Pool *q = &a->pool[j];
			if (q->sec != sec) continue;
			elf_align(a->o, sec, q->size == 8 ? 3 : 2, 0, 0, 0);
			elf_define(a->o, q->label, sec, elf_frag(a->o, sec)->size);
			Val v = q->v;
			if (emit_value(a, (int)q->size, &v)) return -1;
		}
	}
	a->npool = 0;
	return 0;
}

int as_trace;	/* print each instruction statement and its encoding (testing) */

static int instruction(As *a, const char *p, const char *e)
{
	const char *p0 = p;
	const char *s = p;
	while (p < e && (isidc((unsigned char)*p))) p++;
	char mn[32];
	(void)0;
	size_t mlen = (size_t)(p - s);
	if (mlen >= sizeof mn) return err(a, "invalid instruction mnemonic");
	for (size_t i = 0; i < mlen; i++) mn[i] = (char)tolower((unsigned char)s[i]);
	mn[mlen] = 0;
	int names = names_ops(mn, mlen);
	A64Op ops[MAXOPS];
	int nop = 0;
	p = ws(p, e);
	while (p < e) {
		if (nop >= MAXOPS) return err(a, "too many operands");
		A64Op *op = &ops[nop++];
		memset(op, 0, sizeof *op);
		char c = *p;
		if (c == '[') {
			if (parse_mem(a, &p, e, op)) return -1;
		} else if (c == '{') {
			p = ws(p + 1, e);
			op->kind = A64O_LIST;
			int first = 1;
			for (;;) {
				const char *t = p;
				while (p < e && isidc((unsigned char)*p)) p++;
				A64Op r;
				if (!parse_reg(a, t, (size_t)(p - t), &r, 0) || r.kind != A64O_VREG) return err(a, "vector register expected");
				if (first) { op->reg = r.reg; op->arr = r.arr; op->cls = A64_V; op->nregs = 1; first = 0; }
				else op->nregs++;
				p = ws(p, e);
				if (p < e && *p == '-') {
					p = ws(p + 1, e);
					t = p;
					while (p < e && isidc((unsigned char)*p)) p++;
					A64Op r2;
					if (!parse_reg(a, t, (size_t)(p - t), &r2, 0) || r2.kind != A64O_VREG) return err(a, "vector register expected");
					op->nregs = (uint8_t)(((r2.reg - op->reg) & 31) + 1);
					p = ws(p, e);
				}
				if (p < e && *p == ',') { p = ws(p + 1, e); continue; }
				break;
			}
			if (p >= e || *p != '}') return err(a, "'}' expected");
			p = ws(p + 1, e);
			if (p < e && *p == '[') {
				int64_t ix;
				p++;
				if (abs_expr(a, &p, e, &ix)) return -1;
				p = ws(p, e);
				if (p >= e || *p != ']') return err(a, "']' expected");
				p++;
				op->idx = (uint8_t)ix; op->hasidx = 1;
			}
		} else if (c == '#' || c == ':' || c == '(' || c == '-' || c == '+' || c == '~' || isdigit((unsigned char)c)) {
			if (parse_imm(a, &p, e, op)) return -1;
		} else if (isids((unsigned char)c)) {
			const char *t = p;
			while (p < e && isidc((unsigned char)*p)) p++;
			size_t tn = (size_t)(p - t);
			int k;
			const char *q = ws(p, e);
			if (parse_reg(a, t, tn, op, 0)) {
				if (op->kind == A64O_REG && q < e && *q == '!') { op->wb = 1; p = q + 1; }
				else if (op->kind == A64O_VREG && q < e && *q == '[') {
					int64_t ix;
					q++;
					if (abs_expr(a, &q, e, &ix)) return -1;
					q = ws(q, e);
					if (q >= e || *q != ']') return err(a, "']' expected");
					op->kind = A64O_VELEM; op->idx = (uint8_t)ix; op->hasidx = 1;
					p = q + 1;
				}
			} else if ((k = shift_kind(t, tn)) >= 0 && (q >= e || *q == '#' || *q == ',' || *q == '(' || isdigit((unsigned char)*q))) {
				op->kind = A64O_SHIFT; op->shift = (uint8_t)k;
				p = q;
				if (p < e && *p != ',') {
					int64_t amt;
					if (*p == '#') p++;
					if (abs_expr(a, &p, e, &amt)) return -1;
					op->imm = amt; op->hasamt = 1;
				}
			} else if (names && (q >= e || *q == ',')) {
				op->kind = A64O_NAME; op->name = t; op->nlen = (uint32_t)tn;
			} else {
				p = t;
				if (parse_imm(a, &p, e, op)) return -1;
			}
		} else if (c == '=') {
			p++;
			if (parse_imm(a, &p, e, op)) return -1;
			op->kind = A64O_IMM; op->hash = 2;	/* =expr */
		} else if (c == '"' || c == '.') {
			if (parse_imm(a, &p, e, op)) return -1;
		} else return err(a, "unexpected token in operand");
		p = ws(p, e);
		if (p >= e) break;
		if (*p != ',') return err(a, "unexpected token in argument list");
		p = ws(p + 1, e);
	}
	/* ldr Rt, =expr: movz if it fits, else a constant-pool entry */
	if (nop == 2 && ops[1].kind == A64O_IMM && ops[1].hash == 2 && !strcmp(mn, "ldr") && ops[0].kind == A64O_REG) {
		int x = ops[0].cls == A64_X;
		if (!ops[1].hassym) {
			uint64_t imm = (uint64_t)ops[1].imm;
			unsigned sh = 0, maxsh = x ? 48 : 16;
			while (imm > 0xffff && __builtin_ctzll(imm) >= 16) { sh += 16; imm >>= 16; }
			if (sh <= maxsh && imm <= 0xffff) {
				memcpy(mn, "movz", 5); mlen = 4;
				ops[1].imm = (int64_t)imm; ops[1].v.c = (int64_t)imm; ops[1].hash = 1;
				if (sh) { memset(&ops[2], 0, sizeof ops[2]); ops[2].kind = A64O_SHIFT; ops[2].shift = A64_LSL; ops[2].imm = sh; ops[2].hasamt = 1; nop = 3; }
				goto enc;
			}
		}
		uint32_t lab = pool_entry(a, ops[1].v, x ? 8 : 4);
		ops[1].v = (Val){ lab, 0, 0 }; ops[1].hassym = 1; ops[1].hash = 0; ops[1].imm = 0;
	}
enc:;
	uint32_t w;
	A64Fix fx; fx.type = 0;
	const char *msg = 0;
	if (a64_asm(mn, mlen, ops, nop, &w, &fx, &msg)) return err(a, "%s: %s", mn, msg ? msg : "invalid instruction");
	ElfObj *o = a->o;
	elf_code(o, a->cur);
	uint64_t off = elf_frag(o, a->cur)->size;
	uint8_t *q = elf_grow(o, a->cur, 4);
	q[0] = (uint8_t)w; q[1] = (uint8_t)(w >> 8); q[2] = (uint8_t)(w >> 16); q[3] = (uint8_t)(w >> 24);
	if (fx.type) elf_fixup(o, a->cur, off, q, fx.type, fx.v);
	if (as_trace) printf("%08x %d\t%.*s\n", w, fx.type != 0, (int)(e - p0), p0);
	return 0;
}

/* ---------------------------------------------------------------- statements */

static int define_label(As *a, const char *s, size_t n)
{
	if (isdigit((unsigned char)s[0])) {
		uint32_t num = 0;
		for (size_t i = 0; i < n; i++) num = num * 10 + (uint32_t)(s[i] - '0');
		NumLab *nl = numlab(a, num);
		uint32_t y = nl->fwd ? nl->fwd : tmpsym(a);
		nl->fwd = 0;
		nl->cur = y;
		elf_define(a->o, y, a->cur, elf_frag(a->o, a->cur)->size);
		return 0;
	}
	uint32_t y = sym(a, s, n);
	ElfSym *ys = elf_symp(a->o, y);
	if (ys->flags & ESYM_DEFINED) return err(a, "invalid symbol redefinition");
	elf_define(a->o, y, a->cur, elf_frag(a->o, a->cur)->size);
	return 0;
}

static int directive(As *a, Sv w, const char *p, const char *e)
{
	const char *d = w.s + 1;
	size_t n = w.n - 1;
#define IS(x) (n == sizeof(x) - 1 && !memcmp(d, x, n))
	switch (d[0]) {
	case 'a':
		if (IS("align") || IS("p2align")) return d_align(a, p, e, 1);
		if (IS("ascii")) return d_ascii(a, p, e, 0);
		if (IS("asciz")) return d_ascii(a, p, e, 1);
		if (IS("arch") || IS("arch_extension")) return 0;
		break;
	case 'b':
		if (IS("byte")) return d_data(a, p, e, 1);
		if (IS("balign")) return d_align(a, p, e, 0);
		if (IS("bss")) { switch_to(a, std_section(a, ".bss")); return 0; }
		break;
	case 'c':
		if (IS("cpu")) return 0;
		break;
	case 'd':
		if (IS("data")) { switch_to(a, std_section(a, ".data")); return 0; }
		break;
	case 'e':
		if (IS("equ")) goto set;
		if (IS("error")) {
			Sv t = trim(p, e);
			return err(a, "%.*s", (int)t.n, t.s);
		}
		if (IS("exitm")) return d_exitm(a);
		if (IS("extern")) return 0;
		if (IS("endm") || IS("endmacro")) return err(a, "unexpected '%.*s' in file, no current macro definition", (int)w.n, w.s);
		if (IS("endr")) return err(a, "unexpected '.endr' directive, no current .rept");
		if (IS("equiv")) {
			Sv nm;
			if (parse_name(a, &p, e, &nm)) return -1;
			uint32_t y = elf_sym_lookup(a->o, nm.s, nm.n);
			if (y && (elf_symp(a->o, y)->flags & ESYM_DEFINED)) return err(a, "redefinition of '%.*s'", (int)nm.n, nm.s);
			p = ws(p, e);
			if (p < e && *p == ',') p++;
			return assign(a, nm, p, e);
		}
		break;
	case 'f':
		if (IS("fill")) return d_fill(a, p, e);
		if (IS("file")) return 0;
		break;
	case 'g':
		if (IS("globl") || IS("global")) return d_symattr(a, p, e, 'g');
		break;
	case 'h':
		if (IS("hword") || IS("half")) return d_data(a, p, e, 2);
		if (IS("hidden")) return d_symattr(a, p, e, 'h');
		break;
	case 'i':
		if (IS("inst")) return d_inst(a, p, e);
		if (IS("int")) return d_data(a, p, e, 4);
		if (IS("incbin")) return d_incbin(a, p, e);
		if (IS("irp")) return d_irp(a, p, e, 0);
		if (IS("irpc")) return d_irp(a, p, e, 1);
		if (IS("internal")) return d_symattr(a, p, e, 'i');
		if (IS("ident")) return 0;
		break;
	case 'l':
		if (IS("long")) return d_data(a, p, e, 4);
		if (IS("local")) return d_symattr(a, p, e, 'l');
		break;
	case 'm':
		if (IS("macro")) return d_macro(a, p, e);
		break;
	case 'o':
		if (IS("org")) return d_org(a, p, e);
		break;
	case 'p':
		if (IS("pushsection")) return d_section(a, p, e, 1);
		if (IS("popsection")) {
			if (!a->nss) return err(a, ".popsection without corresponding .pushsection");
			a->nss--;
			a->cur = a->ss[a->nss].cur; a->prev = a->ss[a->nss].prev;
			return 0;
		}
		if (IS("previous")) { uint32_t t = a->cur; a->cur = a->prev; a->prev = t; return 0; }
		if (IS("p2align")) return d_align(a, p, e, 1);
		if (IS("protected")) return d_symattr(a, p, e, 'p');
		if (IS("purgem")) return d_purgem(a, p, e);
		if (IS("print")) return 0;
		break;
	case 'q':
		if (IS("quad")) return d_data(a, p, e, 8);
		break;
	case 'r':
		if (IS("rept") || IS("rep")) return d_rept(a, p, e);
		if (IS("rodata")) { switch_to(a, std_section(a, ".rodata")); return 0; }
		break;
	case 's':
		if (IS("section")) return d_section(a, p, e, 0);
		if (IS("set")) goto set;
		if (IS("size")) return d_size(a, p, e);
		if (IS("short")) return d_data(a, p, e, 2);
		if (IS("space") || IS("skip")) return d_space(a, p, e);
		if (IS("string")) return d_ascii(a, p, e, 1);
		if (IS("subsection")) {
			int64_t sub = 0;
			if (ws(p, e) < e && abs_expr(a, &p, e, &sub)) return -1;
			switch_to(a, elf_subsection(a->o, elf_frag(a->o, a->cur)->sec, (int32_t)sub));
			return 0;
		}
		break;
	case 't':
		if (IS("text")) { switch_to(a, std_section(a, ".text")); return 0; }
		if (IS("type")) return d_type(a, p, e);
		break;
	case 'u':
		if (IS("unreq")) {
			Sv nm = first_word(p, e);
			for (uint32_t i = a->nal; i-- > 0;)
				if (a->al[i].name.n == nm.n && !strncasecmp(a->al[i].name.s, nm.s, nm.n)) {
					a->al[i] = a->al[--a->nal];
					return 0;
				}
			return 0;
		}
		break;
	case 'w':
		if (IS("word")) return d_data(a, p, e, 4);
		if (IS("weak")) return d_symattr(a, p, e, 'w');
		if (IS("warning")) return 0;
		break;
	case 'x':
		if (IS("xword")) return d_data(a, p, e, 8);
		break;
	case 'z':
		if (IS("zero")) return d_space(a, p, e);
		break;
	case '2':
		if (IS("2byte")) return d_data(a, p, e, 2);
		break;
	case '4':
		if (IS("4byte")) return d_data(a, p, e, 4);
		break;
	case '8':
		if (IS("8byte")) return d_data(a, p, e, 8);
		break;
	}
	return err(a, "unknown directive '%.*s'", (int)w.n, w.s);
set:;
	Sv nm;
	if (parse_name(a, &p, e, &nm)) return -1;
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected comma");
	return assign(a, nm, p + 1, e);
#undef IS
}

static int statement(As *a, const char *p, const char *e)
{
	p = ws(p, e);
	if (p >= e) return 0;
	Sv w = first_word(p, e);
	int rc;
	if (cond_dir(a, w, w.s + w.n, e, &rc)) return rc;
	if (cond_ignore(a)) return 0;
	/* labels */
	for (;;) {
		w = first_word(p, e);
		if (!w.n) break;
		const char *q = ws(w.s + w.n, e);
		if (q < e && *q == ':') {
			int numeric = 1;
			for (uint32_t i = 0; i < w.n; i++) if (!isdigit((unsigned char)w.s[i])) numeric = 0;
			if (!numeric && isdigit((unsigned char)w.s[0])) break;
			if (define_label(a, w.s, w.n)) return -1;
			p = ws(q + 1, e);
			if (p >= e) return 0;
			continue;
		}
		break;
	}
	w = first_word(p, e);
	if (!w.n) return err(a, "unexpected token at start of statement");
	const char *q = ws(w.s + w.n, e);
	if (q < e && *q == '=' && !(q + 1 < e && q[1] == '=')) return assign(a, w, q + 1, e);
	if (q + 4 <= e && !memcmp(q, ".req", 4) && (q + 4 == e || !isidc((unsigned char)q[4]))) {
		Sv reg = trim(q + 4, e);
		if (a->nal == a->alcap) {
			uint32_t nc = a->alcap ? a->alcap * 2 : 64;
			Alias *t = rget(&a->defs, sizeof *t * nc);
			if (a->nal) memcpy(t, a->al, sizeof *t * a->nal);
			a->al = t; a->alcap = nc;
		}
		a->al[a->nal].name = svdup(a, w.s, w.n);
		a->al[a->nal].reg = svdup(a, reg.s, reg.n);
		a->nal++;
		return 0;
	}
	Macro *m = find_macro(a, w.s, w.n);
	if (m) return invoke(a, m, w.s + w.n, e);
	if (w.s[0] == '.') return directive(a, w, w.s + w.n, e);
	return instruction(a, p, e);
}

/* ---------------------------------------------------------------- driver */

As *as_new(void)
{
	As *a = reserve(sizeof *a);
	a->xs.base = reserve((size_t)1 << 32); a->xs.cap = (size_t)1 << 32;
	a->defs.base = reserve((size_t)1 << 30); a->defs.cap = (size_t)1 << 30;
	a->sb.base = reserve((size_t)1 << 24); a->sb.cap = (size_t)1 << 24;
	return a;
}

void as_free(As *a)
{
	munmap(a->xs.base, a->xs.cap); munmap(a->defs.base, a->defs.cap); munmap(a->sb.base, a->sb.cap);
	munmap(a, sizeof *a);
}

int as_run(As *a, ElfObj *o, const char *text, size_t len, const char *name, const char **msg)
{
	a->o = o; a->nfr = 0; a->ncs = 0; a->nss = 0; a->err = 0; a->msg[0] = 0;
	a->xs.used = 0; a->defs.used = 0; a->ninst = 0; a->nal = 0; a->alcap = 0; a->al = 0;
	a->fname = name;
	a->mmask = 255; a->mcount = 0;
	a->mtab = rget(&a->defs, sizeof(Macro *) * (a->mmask + 1));
	memset(a->mtab, 0, sizeof(Macro *) * (a->mmask + 1));
	a->npool = 0; a->poolcap = 0; a->pool = 0;
	a->vmask = 63; a->vcount = 0;
	a->vars = rget(&a->defs, sizeof(Var) * (a->vmask + 1));
	memset(a->vars, 0, sizeof(Var) * (a->vmask + 1));
	a->nlmask = 255; a->nlcount = 0;
	a->nl = rget(&a->defs, sizeof(NumLab) * (a->nlmask + 1));
	memset(a->nl, 0, sizeof(NumLab) * (a->nlmask + 1));
	elf_reset(o);
	uint32_t text_sec = std_section(a, ".text");
	elf_sec(o, text_sec)->align = 2;
	a->cur = a->prev = text_sec;
	push_frame(a, text, len, FR_FILE, 0);
	while (a->nfr && !a->err) {
		Frame *f = &a->fr[a->nfr - 1];
		if (f->p >= f->e) { pop_frame(a); continue; }
		const char *raw;
		size_t n = next_stmt(a, f, &raw);
		const char *s = (const char *)a->sb.base;
		if (statement(a, s, s + n)) {
			size_t k = strlen(a->msg);
			snprintf(a->msg + k, sizeof a->msg - k, "\n  in: %.*s", (int)(n > 200 ? 200 : n), s);
			break;
		}
		if (o->has_err) { err(a, "%s", o->err); break; }
	}
	if (!a->err && a->ncs) err(a, "unmatched .ifs or .elses");
	if (!a->err && a->npool) flush_pools(a);
	if (!a->err) {
		/* undefined numeric forward references */
		for (uint32_t i = 0; i <= a->nlmask; i++)
			if (a->nl[i].fwd && !(elf_symp(o, a->nl[i].fwd)->flags & ESYM_DEFINED)) { err(a, "directional label undefined"); break; }
	}
	if (!a->err && a->vcount) {
		for (uint32_t i = 0; i <= a->vmask; i++) {
			Var *vr = &a->vars[i];
			if (!vr->sym || vr->v.add == (uint32_t)-1) continue;
			ElfSym *s = elf_symp(o, vr->sym);
			if (s->flags & ESYM_TEMP) continue;
			Val v = vr->v;
			fold(a, &v);
			if (v.add && !v.sub && (elf_symp(o, v.add)->flags & ESYM_DEFINED)) {
				ElfSym *t = elf_symp(o, v.add);
				elf_define(o, vr->sym, t->frag, t->value + (uint64_t)v.c);
				s->alias = t->alias ? t->alias : v.add;
			} else if (!v.add && !v.sub) elf_define_abs(o, vr->sym, (uint64_t)v.c);
			else { err(a, "unsupported variable '%s'", s->name); break; }
		}
	}
	if (a->err) { *msg = a->msg; return -1; }
	return 0;
}
