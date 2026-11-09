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
#include "asimpl.h"

VPRIV Sv svdup(As *a, const char *s, size_t n)
{
	char *p = rget(&a->defs, n + 1);
	memcpy(p, s, n); p[n] = 0;
	return (Sv){ p, (uint32_t)n };
}

VPRIV uint32_t curline(As *a)
{
	for (int i = a->nfr - 1; i >= 0; i--) if (a->fr[i].kind == FR_FILE) return a->fr[i].line;
	return 0;
}

VPRIV int err(As *a, const char *fmt, ...)
{
	if (a->err) return -1;
	a->err = 1;
	int k = snprintf(a->msg, sizeof a->msg, "%s:%u: error: ", a->fname, curline(a));
	va_list ap; va_start(ap, fmt);
	vsnprintf(a->msg + k, sizeof a->msg - k, fmt, ap);
	va_end(ap);
	return -1;
}

/* ---------------------------------------------------------------- symbols */

static uint32_t sym(As *a, const char *s, size_t n)
{
	uint32_t y = elf_sym(a->o, s, (uint32_t)n);
	ElfSym *p = elf_symp(a->o, y);
	if (!p->flags && n >= 2 && s[0] == '.' && s[1] == 'L') p->flags |= ESYM_TEMP;
	return y;
}

VPRIV uint32_t tmpsym(As *a)
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


static int is_abs(As *a, Val *v)
{
	(void)a;
	return !v->add && !v->sub;
}

/* fold add - sub when both are in the same frag (or absolute) */
VPRIV void fold(As *a, Val *v)
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
		while (p < e && c_xdigit((unsigned char)*p)) { x = x * 16 + (uint64_t)(c_digit((unsigned char)*p) ? *p - '0' : (c_lower((unsigned char)*p) - 'a' + 10)); p++; }
		if (p == s) return err(l->a, "invalid hexadecimal number");
	} else if (p + 2 < e && p[0] == '0' && (p[1] == 'b' || p[1] == 'B') && (p[2] == '0' || p[2] == '1')) {
		p += 2;
		while (p < e && (*p == '0' || *p == '1')) x = x * 2 + (uint64_t)(*p++ - '0');
	} else {
		const char *s = p;
		while (p < e && c_digit((unsigned char)*p)) p++;
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
	if (c_digit((unsigned char)c)) return parse_number(l, v);
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
	if (!is_abs(a, x) || !is_abs(a, y)) {
		/* (label difference) op constant, e.g. (end - start) >> 5 with end not yet placed:
		   a temporary symbol that gets its value once the layout is known */
		int diff_x = is_abs(a, x) || (x->add && x->sub), diff_y = is_abs(a, y) || (y->add && y->sub);
		if (!diff_x || !diff_y || !strchr("*/%LR&|^!", op)) return err(a, "expected absolute expression");
		uint32_t t = tmpsym(a);
		elf_expr_sym(a->o, t, op, *x, *y);
		*x = (Val){ t, 0, 0 };
		return 0;
	}
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

VPRIV int abs_expr(As *a, const char **pp, const char *e, int64_t *out)
{
	Lx l = { a, *pp, e };
	Val v;
	if (expr(&l, &v)) return -1;
	*pp = l.p;
	if (!is_abs(a, &v)) return err(a, "expected absolute expression");
	*out = v.c;
	return 0;
}

VPRIV int val_expr(As *a, const char **pp, const char *e, Val *v)
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

/* bytes next_stmt must look at: newline, ';', comments, strings */
static const uint8_t sstop[256] = { ['\n'] = 1, [';'] = 1, ['/'] = 1, ['"'] = 1 };

/* first byte of [p, e) in sstop (or e) */
static inline const char *find_stop(const char *p, const char *e)
{
#ifdef __aarch64__
	const uint8x16_t nl = vdupq_n_u8('\n'), sc = vdupq_n_u8(';'), sl = vdupq_n_u8('/'), qt = vdupq_n_u8('"');
	while (e - p >= 16) {
		uint8x16_t v = vld1q_u8((const uint8_t *)p);
		uint8x16_t m = vorrq_u8(vorrq_u8(vceqq_u8(v, nl), vceqq_u8(v, sc)), vorrq_u8(vceqq_u8(v, sl), vceqq_u8(v, qt)));
		uint64_t bits = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(m), 4)), 0);
		if (bits) return p + (__builtin_ctzll(bits) >> 2);
		p += 16;
	}
#endif
	while (p < e && !sstop[(uint8_t)*p]) p++;
	return p;
}

/* The general case: copy the statement into a->sb without comments. */
static __attribute__((noinline)) size_t next_stmt_copy(As *a, Frame *f)
{
	const char *p = f->p, *e = f->e;
	char *o = (char *)a->sb.base;
	size_t n = 0, cap = a->sb.cap - 1;
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
		if (!sstop[(uint8_t)c]) {	/* plain run: copy it whole */
			const char *q = find_stop(p + 1, e);
			size_t k = (size_t)(q - p);
			if (k > cap - n) k = cap - n;
			memcpy(o + n, p, k); n += k; p = q;
			continue;
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

/* The next statement of f, without comments: *s points at it, either in
 * the frame text (no comment or string in it, the common case) or in
 * a->sb.  Returns its length; sets *raw to where it started in the frame
 * text.  The text is not NUL-terminated. */
static inline __attribute__((always_inline)) size_t next_stmt(As *a, Frame *f, const char **raw, const char **s)
{
	const char *p = f->p, *e = f->e;
	*raw = p;
	const char *q = p;
	int bol = f->bol;
	if (bol) {
		while (q < e && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
		if (q < e && *q != '\n') bol = 0;
		if (q < e && *q == '#') goto slow;
	}
	q = find_stop(q, e);
	if (q < e) {
		if (*q == '\n') { f->line++; bol = 1; }
		else if (*q != ';') goto slow;
		f->p = q + 1;
	} else f->p = q;
	f->bol = (uint8_t)bol;
	*s = p;
	return (size_t)(q - p);
slow:
	*s = (const char *)a->sb.base;
	return next_stmt_copy(a, f);
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

static Sv trim(const char *p, const char *e);
/* .section name [, "flags" [, @type [, entsize]]] ; push = .pushsection */
static int d_section(As *a, const char *p, const char *e, int push)
{
	Sv name;
	if (parse_name(a, &p, e, &name)) return -1;
	uint32_t type = 0; uint64_t flags = 0; uint32_t entsize = 0, group = 0;
	int have_flags = 0, comdat = 0;
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
				case 'G': flags |= SHF_GROUP; break;
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
					if ((flags & SHF_MERGE) && p < e && *p == ',') {
						p++;
						int64_t es;
						if (abs_expr(a, &p, e, &es)) return -1;
						entsize = (uint32_t)es;
						p = ws(p, e);
					}
					if ((flags & SHF_GROUP) && p < e && *p == ',') {
						p++;
						Sv g;
						if (parse_name(a, &p, e, &g)) return -1;
						group = sym(a, g.s, g.n);
						p = ws(p, e);
						if (p < e && *p == ',') {
							Sv c = trim(p + 1, e);
							if (!sveq(c, "comdat")) return err(a, "expected 'comdat'");
							comdat = 1;
						}
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
	if ((flags & SHF_GROUP) && !group) return err(a, "expected group name");
	uint32_t s = elf_section(a->o, name.s, name.n, type, flags, entsize, group);
	if (group) { elf_reg(a->o, group); elf_sec(a->o, s)->comdat = (uint8_t)comdat; }
	{	/* MC: a code section is at least 4-aligned once switched to */
		ElfSec *se = elf_sec(a->o, s);
		if ((se->flags & SHF_EXECINSTR) && se->align < 2) se->align = 2;
	}
	switch_to(a, elf_subsection(a->o, s, (int32_t)sub));
	return 0;
}

/* ---------------------------------------------------------------- data */

VPRIV int emit_value(As *a, int size, Val *v)
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

static Sv first_word(const char *p, const char *e);

/* .octa: 16-byte integer literals (only literals: 128-bit expressions do not exist here) */
static int d_octa(As *a, const char *p, const char *e)
{
	for (p = ws(p, e); p < e;) {
		unsigned __int128 x = 0;
		int neg = 0;
		if (*p == '-') { neg = 1; p++; }
		if (p + 1 < e && p[0] == '0' && (p[1] | 32) == 'x') {
			const char *s = p += 2;
			while (p < e && c_xdigit((unsigned char)*p)) { x = x * 16 + (unsigned)(c_digit((unsigned char)*p) ? *p - '0' : c_lower((unsigned char)*p) - 'a' + 10); p++; }
			if (p == s) return err(a, "invalid hexadecimal number");
		} else if (p < e && c_digit((unsigned char)*p)) {
			while (p < e && c_digit((unsigned char)*p)) x = x * 10 + (unsigned)(*p++ - '0');
		} else
			return err(a, "unexpected token in '.octa' directive");
		if (neg) x = -x;
		elf_data(a->o, a->cur);
		uint8_t *q = elf_grow(a->o, a->cur, 16);
		for (int i = 0; i < 16; i++) q[i] = (uint8_t)(x >> 8 * i);
		p = ws(p, e);
		if (p >= e) break;
		if (*p != ',') return err(a, "unexpected token in directive");
		p = ws(p + 1, e);
	}
	return 0;
}

/* .reloc offset, R_AARCH64_<type>[, expr]: a relocation at a constant offset of the current section */
/* a .reloc fixup goes among its frag's fixups in offset order (MC) */
static void reloc_fixup(ElfObj *o, uint32_t f, uint64_t off, uint8_t *at, uint32_t type, Val v)
{
	elf_fixup(o, f, off, at, type, v);
	ElfFix *fx = (ElfFix *)o->fixes.base;
	ElfFrag *fr = elf_frag(o, f);
	uint32_t id = o->nfix - 1;
	if (fr->fix_head == id) return;
	uint32_t prev = 0, k = fr->fix_head;
	while (k != id && fx[k].off <= off) { prev = k; k = fx[k].next; }
	if (k == id) return;
	/* unlink the tail, insert before k */
	uint32_t t = k;
	while (fx[t].next != id) t = fx[t].next;
	fx[t].next = 0; fr->fix_tail = t;
	fx[id].next = k;
	if (k == fr->fix_head) fr->fix_head = id; else fx[prev].next = id;
}

static int d_reloc(As *a, const char *p, const char *e)
{
	static const struct { const char *n; uint32_t t; } types[] = {
		{ "R_AARCH64_ABS64", R_AARCH64_ABS64 }, { "R_AARCH64_ABS32", R_AARCH64_ABS32 }, { "R_AARCH64_ABS16", R_AARCH64_ABS16 },
		{ "R_AARCH64_PREL64", R_AARCH64_PREL64 }, { "R_AARCH64_PREL32", R_AARCH64_PREL32 }, { "R_AARCH64_PREL16", R_AARCH64_PREL16 },
		{ "R_AARCH64_NONE", R_AARCH64_NONE }, { "R_AARCH64_TLSDESC_CALL", R_AARCH64_TLSDESC_CALL },
		{ "BFD_RELOC_NONE", R_AARCH64_NONE }, { "BFD_RELOC_64", R_AARCH64_ABS64 },
		{ "BFD_RELOC_32", R_AARCH64_ABS32 }, { "BFD_RELOC_16", R_AARCH64_ABS16 },
	};
	ElfObj *o = a->o;
	Val ov;
	if (val_expr(a, &p, e, &ov)) return -1;
	fold(a, &ov);
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected comma");
	Sv nm = first_word(p + 1, e);
	uint32_t type = (uint32_t)-1, size = 0;
	for (size_t i = 0; i < sizeof types / sizeof types[0]; i++)
		if (strlen(types[i].n) == nm.n && !memcmp(types[i].n, nm.s, nm.n)) type = types[i].t;
	if (type == (uint32_t)-1) return err(a, "unknown relocation name");
	size = type == R_AARCH64_NONE || type == R_AARCH64_TLSDESC_CALL ? 0 :
	       type == R_AARCH64_ABS64 || type == R_AARCH64_PREL64 ? 8 : type == R_AARCH64_ABS32 || type == R_AARCH64_PREL32 ? 4 : 2;
	p = ws(nm.s + nm.n, e);
	Val v = { 0, 0, 0 };
	if (p < e && *p == ',') { p++; if (val_expr(a, &p, e, &v)) return -1; }
	uint32_t sec = elf_frag(o, a->cur)->sec;
	uint64_t off;
	if (!ov.add && !ov.sub) off = (uint64_t)ov.c;
	else {
		/* .reloc sym+c: an offset in sym's section (MC: the section of the offset) */
		ElfSym *t = ov.add && !ov.sub ? elf_symp(o, ov.add) : 0;
		if (!t || !(t->flags & ESYM_DEFINED) || (t->flags & ESYM_ABS)) return err(a, ".reloc offset is not absolute or a label");
		if (!size) { reloc_fixup(o, t->frag, t->value + (uint64_t)ov.c, 0, type, v); return 0; }
		sec = elf_frag(o, t->frag)->sec;
		off = t->value + (uint64_t)ov.c;
		for (uint32_t f = sec; f != t->frag; f = elf_frag(o, f)->next) off += elf_frag(o, f)->size;
	}
	/* the frag and bytes at section offset off */
	uint64_t at = 0;
	for (uint32_t f = sec; f; f = elf_frag(o, f)->next) {
		ElfFrag *fr = elf_frag(o, f);
		if (off >= at && off + size <= at + fr->size + (size == 0)) {
			uint64_t k = off - at;
			if (!size) { reloc_fixup(o, f, k, 0, type, v); return 0; }
			for (ElfChunk *ch = fr->head; ch; ch = ch->next) {
				if (k + size <= ch->used) {
					/* llvm-mc keeps the bytes and adds the relocation; the field is not patched */
					uint8_t keep[8];
					memcpy(keep, ch->data + k, size);
					reloc_fixup(o, f, off - at, ch->data + k, type, v);
					memcpy(ch->data + k, keep, size);
					return 0;
				}
				if (k < ch->used) break;
				k -= ch->used;
			}
			break;
		}
		at += fr->size;
	}
	return err(a, ".reloc offset is not inside the section's data");
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
			case 'x': { int v = 0; while (p < e && c_xdigit((unsigned char)*p)) { v = v * 16 + (c_digit((unsigned char)*p) ? *p - '0' : c_lower((unsigned char)*p) - 'a' + 10); p++; } c = (char)v; break; }
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

/* .float/.single (size 4), .double (8): literals, as llvm-mc (inf, nan, signs) */
static int d_float(As *a, const char *p, const char *e, int size)
{
	p = ws(p, e);
	if (p >= e) return 0;
	for (;;) {
		char buf[128];
		size_t k = 0;
		const char *q = p;
		while (q < e && *q != ',' && *q != ' ' && *q != '\t' && k < sizeof buf - 1) buf[k++] = *q++;
		buf[k] = 0;
		char *end;
		const char *s = buf;
		int neg = 0;
		if (*s == '-' || *s == '+') neg = *s++ == '-';
		double d;
		if (!strcasecmp(s, "inf") || !strcasecmp(s, "infinity")) d = __builtin_inf();
		else if (!strcasecmp(s, "nan")) d = __builtin_nan("");
		else {
			d = strtod(s, &end);
			if (end == s || *end) return err(a, "invalid floating point literal");
		}
		uint64_t bits;
		if (__builtin_isnan(d))	/* llvm-mc: every mantissa bit set */
			bits = size == 4 ? 0x7fffffffu | (uint64_t)neg << 31 : 0x7fffffffffffffffu | (uint64_t)neg << 63;
		else if (size == 4) {	/* rounded once, from the text */
			float f = __builtin_isinf(d) || __builtin_isnan(d) ? (float)d : strtof(s, NULL);
			uint32_t b32;
			if (neg) f = -f;
			memcpy(&b32, &f, 4);
			bits = b32;
		} else {
			if (neg) d = -d;
			memcpy(&bits, &d, 8);
		}
		Val v = { 0, 0, (int64_t)bits };
		if (emit_value(a, size, &v)) return -1;
		p = ws(q, e);
		if (p >= e) return 0;
		if (*p != ',') return err(a, "unexpected token in directive");
		p = ws(p + 1, e);
	}
}

/* .base64 "..." */
static int d_base64(As *a, const char *p, const char *e)
{
	char buf[4096];
	size_t n;
	if ((size_t)(e - p) > sizeof buf) return err(a, "string too long");
	if (parse_string(a, &p, e, buf, &n)) return -1;
	if (!n || n % 4) return err(a, "failed to base64 decode string data");
	uint8_t out[3072];
	size_t m = 0;
	for (size_t i = 0; i < n; i += 4) {
		uint32_t w = 0;
		int pad = 0;
		for (int j = 0; j < 4; j++) {
			char c = buf[i + j];
			int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 :
				c == '+' ? 62 : c == '/' ? 63 : c == '=' && i + 4 == n && j >= 2 ? (pad++, 0) : -1;
			if (v < 0 || (pad && c != '=')) return err(a, "failed to base64 decode string data");
			w = w << 6 | (uint32_t)v;
		}
		out[m++] = (uint8_t)(w >> 16);
		if (pad < 2) out[m++] = (uint8_t)(w >> 8);
		if (pad < 1) out[m++] = (uint8_t)w;
	}
	elf_data(a->o, a->cur);
	elf_bytes(a->o, a->cur, out, m);
	return 0;
}

static int code_section(As *a) { return (elf_sec(a->o, elf_frag(a->o, a->cur)->sec)->flags & SHF_EXECINSTR) != 0; }

/* .align/.p2align (p2 = 1) or .balign (p2 = 0); .balignw/.p2alignl..: a
 * fill of fsz bytes (little-endian pattern) */
static int d_align_w(As *a, const char *p, const char *e, int p2, int fsz);
static int d_align(As *a, const char *p, const char *e, int p2) { return d_align_w(a, p, e, p2, 1); }
static int d_align_w(As *a, const char *p, const char *e, int p2, int fsz)
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
	if (fsz == 1) {
		elf_align(a->o, a->cur, l2, !hasfill && code_section(a), (uint8_t)fill, (uint64_t)max);
		return 0;
	}
	ElfFrag *f = elf_frag(a->o, a->cur);
	uint64_t al = (uint64_t)1 << l2, pad = (al - (f->size & (al - 1))) & (al - 1);
	ElfSec *sc = elf_sec(a->o, f->sec);
	if (!hasfill || sc->type == SHT_NOBITS) {
		elf_align(a->o, a->cur, l2, !hasfill && code_section(a), 0, (uint64_t)max);
		return 0;
	}
	/* the pattern repeated over the whole pad (GNU as; llvm-mc lays out the
	 * whole pad but writes only whole values when the pad is not a multiple) */
	if (l2 > sc->align) sc->align = l2;
	if (f->sub) f->has_align |= 1u << l2;
	if (pad && (!max || pad <= (uint64_t)max)) {
		uint8_t *q = elf_grow(a->o, a->cur, pad);
		for (uint64_t i = 0; i < pad; i++) q[i] = (uint8_t)((uint64_t)fill >> 8 * (i % (uint64_t)fsz));
	}
	return 0;
}

/* .comm name, size[, align] (SHN_COMMON, or .bss when the name is local) and .lcomm */
static int d_comm(As *a, const char *p, const char *e, int local)
{
	Sv nm;
	int64_t size, al = 0;
	if (parse_name(a, &p, e, &nm)) return -1;
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected a comma");
	p++;
	if (abs_expr(a, &p, e, &size)) return -1;
	p = ws(p, e);
	if (p < e && *p == ',') { p++; if (abs_expr(a, &p, e, &al)) return -1; }
	if (size < 0) return err(a, "invalid '.comm' or '.lcomm' directive size");
	if (al < 0 || (al & (al - 1))) return err(a, "alignment must be a power of 2");
	uint32_t y = sym(a, nm.s, nm.n);
	ElfSym *s = elf_symp(a->o, y);
	if (s->flags & ESYM_DEFINED) return err(a, "invalid symbol redefinition");
	elf_reg(a->o, y);
	s->type = STT_OBJECT;
	s->size = (uint64_t)size;
	if (local || ((s->flags & ESYM_BINDSET) && s->bind == STB_LOCAL)) {
		uint32_t f = std_section(a, ".bss");
		if (al > 1) elf_align(a->o, f, (unsigned)__builtin_ctzll((uint64_t)al), 0, 0, 0);
		elf_define(a->o, y, f, elf_frag(a->o, f)->size);
		elf_data(a->o, f);
		elf_fill(a->o, f, (uint64_t)size, 0);
		return 0;
	}
	s->common = 1;
	s->value = (uint64_t)(al ? al : 1);
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
	int fd;
	if (a->dir && path[0] != '/') {
		char full[2048];
		snprintf(full, sizeof full, "%s/%s", a->dir, path);
		fd = open(full, O_RDONLY | O_CLOEXEC);
	} else
		fd = open(path, O_RDONLY | O_CLOEXEC);
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
	else if (sveq(t, "gnu_indirect_function") || sveq(t, "STT_GNU_IFUNC")) ty = STT_GNU_IFUNC;
	else if (sveq(t, "common") || sveq(t, "STT_COMMON")) ty = STT_OBJECT;	/* MC: STT_COMMON is written as OBJECT */
	else if (sveq(t, "gnu_unique_object")) { ty = STT_OBJECT; elf_bind(a->o, y, 10); }	/* STB_GNU_UNIQUE */
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
		const char *raw, *s;
		size_t n = next_stmt(a, f, &raw, &s);
		const char *t = ws(s, s + n);
		if (t == s + n || *t != '.') continue;
		Sv w = first_word(t, s + n);
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

/* macro_arg when the argument is a contiguous piece of the text (no
 * string, no blanks around an operator, no error): *out points into the
 * text.  Returns 0 if macro_arg must do it. */
static int macro_arg_plain(const char **pp, const char *e, int vararg, Sv *out)
{
	const char *p = *pp, *end;
	if (vararg) {
		p = ws(p, e);
		const char *q = e;
		while (q > p && (q[-1] == ' ' || q[-1] == '\t')) q--;
		*pp = e;
		*out = (Sv){ p, (uint32_t)(q - p) };
		return 1;
	}
	const char *s = p;
	int depth = 0;
	for (;;) {
		if (depth == 0) {
			if (p < e && *p == ',') { end = p; break; }
			if (p < e && (*p == ' ' || *p == '\t')) {
				end = p;
				p = ws(p, e);
				if (p < e && is_op_start(p, e)) return 0;
				break;
			}
			if (p < e && is_op_start(p, e)) {
				if (*p == '=' && p + 1 < e && p[1] != '=') return 0;
				p += op_len(p, e);
				if (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' || *p == '\v')) return 0;
				continue;
			}
		}
		if (p >= e) { end = p; break; }
		char c = *p;
		if (c == '(') depth++;
		else if (c == ')' && depth) depth--;
		if (c == '"') return 0;
		if (c == ' ' || c == '\t') { p = ws(p, e); continue; }
		if (isidc((unsigned char)c)) { while (p < e && isidc((unsigned char)*p)) p++; continue; }
		if (c == '=' && depth == 0) return 0;
		p++;
	}
	if (depth) return 0;
	*pp = p;
	*out = (Sv){ s, (uint32_t)(end - s) };
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

/* expandMacro: body with \param, \@, \+, \() substitutions.  The body is
 * compiled once into pieces (r is the region for them), then each
 * expansion is copies. */
static Tmpl tmpl_compile(As *a, vregion *r, Sv body, const Param *ps, uint32_t np, int at)
{
	const char *b = body.s;
	size_t n = body.n, nbs = 0;
	for (const char *q = b; (q = memchr(q, '\\', (size_t)(b + n - q))); q++) nbs++;
	Tmpl t = { rget(r, sizeof(Piece) * (nbs + 1)), 0 };
	size_t lit = 0, i = 0;
	while (i < n) {
		const char *q = memchr(b + i, '\\', n - i);
		if (!q) break;
		i = (size_t)(q - b);
		if (i + 1 == n) break;
		int32_t k;
		size_t skip;
		if (at && b[i + 1] == '@') k = -2, skip = 2;
		else if (b[i + 1] == '+') k = -3, skip = 2;
		else if (b[i + 1] == '(' && i + 2 < n && b[i + 2] == ')') k = -1, skip = 3;
		else {
			size_t pos = i + 1, j = pos;
			while (j < n && isidc((unsigned char)b[j])) j++;
			uint32_t m;
			for (m = 0; m < np; m++) if (ps[m].name.n == j - pos && !memcmp(ps[m].name.s, b + pos, j - pos)) break;
			if (m == np) { i = pos; continue; }	/* '\' and the name stay as they are */
			k = (int32_t)m, skip = j - i;
		}
		t.p[t.n++] = (Piece){ (uint32_t)lit, (uint32_t)(i - lit), k };
		i += skip;
		lit = i;
	}
	if (lit < n) t.p[t.n++] = (Piece){ (uint32_t)lit, (uint32_t)(n - lit), -1 };
	(void)a;
	return t;
}

/* .altmacro: a parameter name is replaced wherever it stands as a whole
 * identifier, with or without the backslash */
static Tmpl tmpl_compile_alt(vregion *r, Sv body, const Param *ps, uint32_t np)
{
	const char *b = body.s;
	size_t n = body.n;
	Tmpl t = { rget(r, sizeof(Piece) * (n / 2 + 2)), 0 };
	size_t lit = 0, i = 0;
	while (i < n) {
		size_t st = i;
		int bs = b[i] == '\\';
		if (bs) i++;
		if (i < n && isidc((unsigned char)b[i]) && (bs || st == 0 || !isidc((unsigned char)b[st - 1]))) {
			size_t j = i;
			while (j < n && isidc((unsigned char)b[j])) j++;
			uint32_t m;
			for (m = 0; m < np; m++) if (ps[m].name.n == j - i && !memcmp(ps[m].name.s, b + i, j - i)) break;
			if (m < np) {
				t.p[t.n++] = (Piece){ (uint32_t)lit, (uint32_t)(st - lit), (int32_t)m };
				lit = j;
			}
			i = j;
			continue;
		}
		if (!bs) i++;
	}
	if (lit < n) t.p[t.n++] = (Piece){ (uint32_t)lit, (uint32_t)(n - lit), -1 };
	return t;
}

/* an .altmacro argument: %expr is its value in decimal, <text> is text with
 * !c standing for c */
static int alt_arg(As *a, const char **pp, const char *e, Sv *out)
{
	const char *p = *pp;
	if (*p == '%') {
		p++;
		int64_t v;
		if (abs_expr(a, &p, e, &v)) return -1;
		char buf[24];
		int k = snprintf(buf, sizeof buf, "%lld", (long long)v);
		*out = svdup(a, buf, (size_t)k);
	} else {
		char *o = rget(&a->defs, (size_t)(e - p) + 1);
		size_t n = 0;
		for (p++; p < e && *p != '>'; p++) {
			if (*p == '!' && p + 1 < e) p++;
			o[n++] = *p;
		}
		if (p >= e) return err(a, "expected '>'");
		p++;
		*out = (Sv){ o, (uint32_t)n };
	}
	*pp = p;
	return 0;
}

static char *utoa(char *o, uint32_t v)
{
	char tmp[10]; int k = 0;
	do tmp[k++] = (char)('0' + v % 10), v /= 10; while (v);
	while (k) *o++ = tmp[--k];
	return o;
}

/* copy n bytes to o, which may be overwritten up to 16 bytes past n (the
 * expansion stack has room); short copies are one 16-byte load when that
 * stays in the source's page */
static inline void xcopy(char *o, const char *s, size_t n)
{
#ifdef __aarch64__
	if (n <= 16 && ((uintptr_t)s & 4095) <= 4096 - 16) { vst1q_u8((uint8_t *)o, vld1q_u8((const uint8_t *)s)); return; }
#endif
	memcpy(o, s, n);
}

static size_t expand(As *a, char *o, const char *b, Tmpl t, const Sv *args, uint32_t count)
{
	char *o0 = o;
	for (uint32_t i = 0; i < t.n; i++) {
		const Piece *p = &t.p[i];
		xcopy(o, b + p->off, p->len); o += p->len;
		if (p->k >= 0) { xcopy(o, args[p->k].s, args[p->k].n); o += args[p->k].n; }
		else if (p->k == -2) o = utoa(o, a->ninst);
		else if (p->k == -3) o = utoa(o, count);
	}
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
	/* names from file text (not a->sb) stay valid; others are copied */
	int keep = a->fr[a->nfr - 1].kind == FR_FILE && !((const uint8_t *)p >= a->sb.base && (const uint8_t *)p < a->sb.base + a->sb.cap);
	p = ws(p, e);
	const char *s = p;
	while (p < e && isidc((unsigned char)*p)) p++;
	if (p == s) return err(a, "expected identifier in '.macro' directive");
	name = keep ? (Sv){ s, (uint32_t)(p - s) } : svdup(a, s, (size_t)(p - s));
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
		q->name = keep ? (Sv){ s, (uint32_t)(p - s) } : svdup(a, s, (size_t)(p - s));
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
	/* file text outlives the macro (as_snippet's contract); expansions do not */
	m->body = a->fr[a->nfr - 1].kind == FR_FILE ? body : svdup(a, body.s, body.n);
	m->count = 0;
	m->t = (Tmpl){ 0, 0 };
	if (a->mcount * 2 >= a->mmask) {
		uint32_t nm = a->mmask * 2 + 1;
		Macro **t = rget(&a->defs, sizeof *t * (nm + 1));
		memset(t, 0, sizeof *t * (nm + 1));
		for (uint32_t i = 0; i <= a->mmask; i++)
			for (Macro *x = a->mtab[i], *nx; x; x = nx) { nx = x->next; uint32_t h = hsv(x->name.s, x->name.n) & nm; x->next = t[h]; t[h] = x; }
		a->mtab = t; a->mmask = nm;
	}
	uint32_t h = hsv(name.s, name.n) & a->mmask;
	for (Macro *x = a->mtab[h]; x; x = x->next)
		if (x->name.n == name.n && !memcmp(x->name.s, name.s, name.n)) return err(a, "macro '%.*s' is already defined", (int)name.n, name.s);
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
			/* the arguments are used before the statement text changes */
			int va = hasvararg && k == np - 1;
			if (a->altmacro && p < e && (*p == '%' || *p == '<')) { if (alt_arg(a, &p, e, &args[k])) return -1; }
			else if (!macro_arg_plain(&p, e, va, &args[k]) && macro_arg(a, &p, e, va, &args[k])) return -1;
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
	Tmpl tt;
	if (a->altmacro) tt = tmpl_compile_alt(&a->defs, m->body, m->params, np);
	else {
		if (!m->t.p) m->t = tmpl_compile(a, &a->defs, m->body, m->params, np, 1);
		tt = m->t;
	}
	char *o = (char *)rget(&a->xs, expand_bound(m->body, args, np));
	size_t n = expand(a, o, m->body.s, tt, args, m->count);
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
	Tmpl t = tmpl_compile(a, &a->xs, body, 0, 0, 0);
	char *o = (char *)rget(&a->xs, expand_bound(body, 0, 0) * (size_t)(cnt ? cnt : 1));
	size_t n = 0;
	for (int64_t i = 0; i < cnt; i++) n += expand(a, o + n, body.s, t, 0, 0);
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
	Tmpl t = tmpl_compile(a, &a->xs, body, &par, 1, 1);
	char *o = (char *)rget(&a->xs, tot + 1);
	size_t n = 0;
	for (uint32_t i = 0; i < nv; i++) n += expand(a, o + n, body.s, t, &vals[i], 0);
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
	if (w.n < 3 || w.s[0] != '.' || (w.s[1] != 'e' && w.s[1] != 'i')) return 0;
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

/* ---------------------------------------------------------------- statements */

static int define_label(As *a, const char *s, size_t n)
{
	if (c_digit((unsigned char)s[0])) {
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

static int d_symver(As *a, const char *p, const char *e);
static int directive(As *a, Sv w, const char *p, const char *e)
{
	const char *d = w.s + 1;
	size_t n = w.n - 1;
#define IS(x) (n == sizeof(x) - 1 && !memcmp(d, x, n))
	switch (d[0]) {
	case 'a':
		if (IS("altmacro")) { a->altmacro = 1; return 0; }
		if (IS("addrsig")) { a->o->has_addrsig = 1; return 0; }
		if (IS("addrsig_sym")) {
			Sv nm;
			if (parse_name(a, &p, e, &nm)) return -1;
			if (a->nasg == a->asgcap) {
				uint32_t nc = a->asgcap ? a->asgcap * 2 : 64;
				uint32_t *t = rget(&a->defs, sizeof *t * nc);
				if (a->nasg) memcpy(t, a->asg, sizeof *t * a->nasg);
				a->asg = t; a->asgcap = nc;
			}
			a->asg[a->nasg++] = sym(a, nm.s, nm.n);
			return 0;
		}
		if (IS("align") || IS("p2align")) return d_align(a, p, e, 1);
		if (IS("ascii")) return d_ascii(a, p, e, 0);
		if (IS("asciz")) return d_ascii(a, p, e, 1);
		if (IS("arch") || IS("arch_extension")) return 0;
		break;
	case 'b':
		if (IS("byte")) return d_data(a, p, e, 1);
		if (IS("balign")) return d_align(a, p, e, 0);
		if (IS("balignw")) return d_align_w(a, p, e, 0, 2);
		if (IS("balignl")) return d_align_w(a, p, e, 0, 4);
		if (IS("base64")) return d_base64(a, p, e);
		if (IS("bss")) { switch_to(a, std_section(a, ".bss")); return 0; }
		break;
	case 'c':
		if (IS("comm")) return d_comm(a, p, e, 0);
		if (IS("cpu")) return 0;
		break;
	case 'd':
		if (IS("data")) { switch_to(a, std_section(a, ".data")); return 0; }
		if (IS("double") || IS("dc.d")) return d_float(a, p, e, 8);
		if (IS("dc.s")) return d_float(a, p, e, 4);
		if (IS("dc.b")) return d_data(a, p, e, 1);
		if (IS("dc") || IS("dc.w")) return d_data(a, p, e, 2);
		if (IS("dc.l")) return d_data(a, p, e, 4);
		if (IS("dc.a")) return d_data(a, p, e, 8);
		break;
	case 'e':
		if (IS("equ")) goto set;
		if (IS("end")) { a->ended = 1; return 0; }
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
		if (IS("float")) return d_float(a, p, e, 4);
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
		if (IS("ident")) {
			char buf[4096];
			size_t k;
			if ((size_t)(e - p) > sizeof buf) return err(a, "string too long");
			if (parse_string(a, &p, e, buf, &k)) return -1;
			/* MC's emitIdent: into .comment at once, a NUL before the first */
			uint32_t s = elf_section(a->o, ".comment", 8, SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 1, 0);
			elf_data(a->o, s);
			if (!a->nident++) elf_fill(a->o, s, 1, 0);
			elf_bytes(a->o, s, buf, k);
			elf_fill(a->o, s, 1, 0);
			return 0;
		}
		break;
	case 'l':
		if (IS("lcomm")) return d_comm(a, p, e, 1);
		if (IS("long")) return d_data(a, p, e, 4);
		if (IS("local")) return d_symattr(a, p, e, 'l');
		if (IS("ltorg") || IS("pool")) return ltorg(a);
		break;
	case 'm':
		if (IS("macro")) return d_macro(a, p, e);
		break;
	case 'n':
		if (IS("noaltmacro")) { a->altmacro = 0; return 0; }
		break;
	case 'o':
		if (IS("org")) return d_org(a, p, e);
		if (IS("octa")) return d_octa(a, p, e);
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
		if (IS("p2alignw")) return d_align_w(a, p, e, 1, 2);
		if (IS("p2alignl")) return d_align_w(a, p, e, 1, 4);
		if (IS("protected")) return d_symattr(a, p, e, 'p');
		if (IS("purgem")) return d_purgem(a, p, e);
		if (IS("print")) {
			char buf[4096];
			size_t k;
			if ((size_t)(e - p) > sizeof buf) return err(a, "string too long");
			if (parse_string(a, &p, e, buf, &k)) return -1;
			printf("%.*s\n", (int)k, buf);	/* llvm-mc: to stdout */
			return 0;
		}
		break;
	case 'q':
		if (IS("quad")) return d_data(a, p, e, 8);
		break;
	case 'r':
		if (IS("rept") || IS("rep")) return d_rept(a, p, e);
		if (IS("reloc")) return d_reloc(a, p, e);
		if (IS("rodata")) { switch_to(a, std_section(a, ".rodata")); return 0; }
		break;
	case 's':
		if (IS("section")) return d_section(a, p, e, 0);
		if (IS("set")) goto set;
		if (IS("size")) return d_size(a, p, e);
		if (IS("symver")) return d_symver(a, p, e);
		if (IS("short")) return d_data(a, p, e, 2);
		if (IS("single")) return d_float(a, p, e, 4);
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
		if (IS("tlsdesccall")) {	/* R_AARCH64_TLSDESC_CALL on the next instruction */
			Val v;
			if (val_expr(a, &p, e, &v)) return -1;
			elf_fixup(a->o, a->cur, elf_frag(a->o, a->cur)->size, 0, R_AARCH64_TLSDESC_CALL, v);
			if (v.add) elf_symp(a->o, v.add)->type = STT_TLS;
			return 0;
		}
		if (IS("text")) { switch_to(a, std_section(a, ".text")); return 0; }
		if (IS("type")) return d_type(a, p, e);
		break;
	case 'u':
		if (IS("unreq")) {
			Sv nm = first_word(p, e);
			for (uint32_t i = a->nal; i-- > 0;)
				if (a->al[i].name.n == nm.n && !strncasecmp(a->al[i].name.s, nm.s, nm.n)) {
					a->al[i] = a->al[--a->nal];
					al_rehash(a);
					return 0;
				}
			return 0;
		}
		break;
	case 'v':
		if (IS("variant_pcs")) {	/* STO_AARCH64_VARIANT_PCS */
			Sv nm;
			if (parse_name(a, &p, e, &nm)) return -1;
			uint32_t y = sym(a, nm.s, nm.n);
			elf_symp(a->o, y)->vis |= 0x80;
			elf_reg(a->o, y);
			return 0;
		}
		break;
	case 'w':
		if (IS("weakref")) {	/* .weakref alias, target */
			Sv al, tg;
			if (parse_name(a, &p, e, &al)) return -1;
			p = ws(p, e);
			if (p >= e || *p != ',') return err(a, "expected a comma");
			p++;
			if (parse_name(a, &p, e, &tg)) return -1;
			uint32_t ya = sym(a, al.s, al.n), yt = sym(a, tg.s, tg.n);
			if (a->nsv == a->svcap) {
				uint32_t nc = a->svcap ? a->svcap * 2 : 16;
				void *t = rget(&a->defs, sizeof *a->sv * nc);
				if (a->nsv) memcpy(t, a->sv, sizeof *a->sv * a->nsv);
				a->sv = t; a->svcap = nc;
			}
			a->sv[a->nsv].orig = ya; a->sv[a->nsv].ver = yt; a->sv[a->nsv].rm = 2;
			a->nsv++;
			return 0;
		}
		if (IS("word")) return d_data(a, p, e, 4);
		if (IS("weak")) return d_symattr(a, p, e, 'w');
		if (IS("warning")) {
			Sv t = trim(p, e);
			char buf[4096];
			size_t k = 0;
			const char *q = t.s;
			if (t.n && *q == '"' && (size_t)(e - q) <= sizeof buf && !parse_string(a, &q, e, buf, &k))
				fprintf(stderr, "%s: warning: %.*s\n", a->fname ? a->fname : "<stdin>", (int)k, buf);
			else
				fprintf(stderr, "%s: warning: .warning directive invoked in source file\n", a->fname ? a->fname : "<stdin>");
			return 0;
		}
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
	int rc, lab = 0;
	if (w.s[0] == '.' && cond_dir(a, w, w.s + w.n, e, &rc)) return rc;
	if (cond_ignore(a)) return 0;
	/* labels */
	const char *q;
	for (;;) {
		q = ws(w.s + w.n, e);
		if (!w.n) break;
		if (q < e && *q == ':') {
			int numeric = 1;
			for (uint32_t i = 0; i < w.n; i++) if (!c_digit((unsigned char)w.s[i])) numeric = 0;
			if (!numeric && c_digit((unsigned char)w.s[0])) break;
			if (define_label(a, w.s, w.n)) return -1;
			p = ws(q + 1, e);
			if (p >= e) return 0;
			w = first_word(p, e);
			lab = 1;
			continue;
		}
		break;
	}
	if (!w.n) return err(a, "unexpected token at start of statement");
	if (lab && w.s[0] == '.' && cond_dir(a, w, w.s + w.n, e, &rc)) return rc;	/* "2: .if ..." */
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
		uint32_t h = al_hash(w.s, w.n);
		a->al[a->nal].next = a->alh[h]; a->alh[h] = a->nal + 1;
		a->nal++;
		return 0;
	}
	Macro *m = find_macro(a, w.s, w.n);
	if (m) return invoke(a, m, w.s + w.n, e);
	if (w.s[0] == '.') return directive(a, w, w.s + w.n, e);
	return instruction(a, w.s, e, w.n);
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

void as_stats(const As *a, uint64_t *stmts, uint64_t *bytes) { *stmts = a->nstmt; *bytes = a->nstmtb; }

void as_free(As *a)
{
	munmap(a->xs.base, a->xs.cap); munmap(a->defs.base, a->defs.cap); munmap(a->sb.base, a->sb.cap);
	munmap(a, sizeof *a);
}

void as_set_dir(As *a, const char *dir) { a->dir = dir; }

/*
 * A persistent assembler bound to one ElfObj for a whole TU: as_begin()
 * resets both, as_snippet() runs statements in the current section
 * (macros, .set variables, numeric labels and the section stack persist
 * across snippets, as in one source file), as_end() flushes literal pools
 * and resolves variables.  The text of every snippet must stay valid until
 * as_end() (macro bodies point into it).  as_run() is the three in one.
 */
void as_begin(As *a, ElfObj *o, const char *name)
{
	a->o = o; a->nfr = 0; a->ncs = 0; a->nss = 0; a->err = 0; a->msg[0] = 0;
	a->xs.used = 0; a->defs.used = 0; a->ninst = 0; a->nal = 0; a->alcap = 0; a->al = 0;
	memset(a->alh, 0, sizeof a->alh);
	a->fname = name;
	a->nident = 0; a->ended = 0; a->altmacro = 0; a->nsv = a->svcap = 0; a->nasg = a->asgcap = 0;
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
}

int as_snippet(As *a, const char *text, size_t len)
{
	ElfObj *o = a->o;
	if (a->err) return -1;
	push_frame(a, text, len, FR_FILE, 0);
	while (a->nfr && !a->err) {
		Frame *f = &a->fr[a->nfr - 1];
		if (f->p >= f->e) { pop_frame(a); continue; }
		const char *raw, *s;
		size_t n = next_stmt(a, f, &raw, &s);
		a->nstmt++; a->nstmtb += n + 1;
		if (statement(a, s, s + n)) {
			size_t k = strlen(a->msg);
			snprintf(a->msg + k, sizeof a->msg - k, "\n  in: %.*s", (int)(n > 200 ? 200 : n), s);
			break;
		}
		if (o->has_err) { err(a, "%s", o->err); break; }
		if (a->ended) break;	/* .end: the rest of the input is ignored */
	}
	a->nfr = 0;
	return a->err ? -1 : 0;
}

/* .symver orig, name@[@[@]]ver[, remove|local|hidden]: an alias defined at
   the end (MC's ELFObjectWriter::executePostLayoutBinding) */
static int d_symver(As *a, const char *p, const char *e)
{
	Sv o, v;
	if (parse_name(a, &p, e, &o)) return -1;
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected a comma");
	p = ws(p + 1, e);
	const char *q = p;
	while (q < e && *q != ',' && *q != ' ' && *q != '\t') q++;
	v = (Sv){ p, (size_t)(q - p) };
	const char *at = memchr(v.s, '@', v.n);
	if (!at) return err(a, "expected a '@' in the name");
	int rm = 0;
	p = ws(q, e);
	if (p < e && *p == ',') {
		Sv w = trim(p + 1, e);
		if (w.n == 6 && !memcmp(w.s, "remove", 6)) rm = 1;
		else if (!(w.n == 5 && !memcmp(w.s, "local", 5)) && !(w.n == 6 && !memcmp(w.s, "hidden", 6)))
			return err(a, "expected 'remove'");
	}
	uint32_t yo = sym(a, o.s, o.n);
	elf_reg(a->o, yo);
	uint32_t yv = sym(a, v.s, v.n);
	elf_reg(a->o, yv);
	if (a->nsv == a->svcap) {
		uint32_t nc = a->svcap ? a->svcap * 2 : 16;
		void *t = rget(&a->defs, sizeof *a->sv * nc);
		if (a->nsv) memcpy(t, a->sv, sizeof *a->sv * a->nsv);
		a->sv = t; a->svcap = nc;
	}
	a->sv[a->nsv].orig = yo; a->sv[a->nsv].ver = yv; a->sv[a->nsv].rm = rm;
	a->nsv++;
	return 0;
}

static void symver_finish(As *a)
{
	ElfObj *o = a->o;
	for (uint32_t i = 0; i < a->nsv; i++) {
		uint32_t yo = a->sv[i].orig, yv = a->sv[i].ver;
		ElfSym *s = elf_symp(o, yo), *t = elf_symp(o, yv);
		if (a->sv[i].rm == 2) {	/* .weakref yo -> yv */
			if (t->flags & ESYM_DEFINED) {
				elf_define(o, yo, t->frag, t->value);
				s->flags |= (t->flags & ESYM_ABS) | ESYM_NOSYMTAB;
				s->flags &= (uint8_t)~ESYM_USED;
				s->alias = t->alias ? t->alias : yv;
				continue;
			}
			/* only referenced through the alias: weak */
			if (!t->reg && !(t->flags & ESYM_BINDSET)) t->bind = STB_WEAK;
			ElfFix *fx = (ElfFix *)o->fixes.base;
			for (uint32_t k = 0; k < o->nfix; k++) {
				if (fx[k].v.add == yo) { fx[k].v.add = yv; elf_reg(o, yv); }
				if (fx[k].v.sub == yo) { fx[k].v.sub = yv; elf_reg(o, yv); }
			}
			s->flags = (uint8_t)((s->flags & ~ESYM_USED) | ESYM_NOSYMTAB);
			continue;
		}
		int def = (s->flags & ESYM_DEFINED) != 0, rm = (int)a->sv[i].rm;
		const char *at = memchr(t->name, '@', t->len);
		if (at && t->len - (size_t)(at - t->name) >= 3 && !memcmp(at, "@@@", 3)) {
			/* name@@@v: @@ when defined here, @ otherwise */
			size_t k = (size_t)(at - t->name);
			char *n = rget(&a->defs, t->len);
			memcpy(n, t->name, k + 1);
			if (def) { memcpy(n + k + 1, at + 2, t->len - k - 2); }
			else { memcpy(n + k + 1, at + 3, t->len - k - 3); }
			t->name = n; t->len -= def ? 1 : 2;
			rm = 1;
		}
		if (def) {
			elf_define(o, yv, s->frag, s->value);
			t->flags |= s->flags & ESYM_ABS;
			t->bind = s->bind; t->type = s->type; t->vis = s->vis;
			t->alias = s->alias ? s->alias : yo;
			t->flags |= ESYM_BINDSET;
		}
		if (!def || rm) {
			/* references go to the versioned name; the original disappears */
			ElfFix *fx = (ElfFix *)o->fixes.base;
			for (uint32_t k = 0; k < o->nfix; k++) {
				if (fx[k].v.add == yo) fx[k].v.add = yv;
				if (fx[k].v.sub == yo) fx[k].v.sub = yv;
			}
			if (s->flags & ESYM_USED) t->flags |= ESYM_USED;
			s->flags = (uint8_t)((s->flags & ~ESYM_USED) | ESYM_NOSYMTAB);
		}
	}
}

int as_end(As *a)
{
	ElfObj *o = a->o;
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
	if (!a->err && a->nsv) symver_finish(a);
	if (!a->err && a->nasg) {
		o->addrsig = a->asg;	/* in a->defs: lives until the next run, after the object is written */
		o->naddrsig = a->nasg;
	}
	return a->err ? -1 : 0;
}

const char *as_msg(const As *a) { return a->msg; }
uint32_t as_frag(const As *a) { return a->cur; }
int as_failed(const As *a) { return a->err; }

/* a symbol as an operand names it (".L" names are temporary) */
uint32_t as_sym(As *a, const char *name, size_t len) { return sym(a, name, len); }

/* sym + c as an operand expression: .set variables substituted, folded */
ElfVal as_val(As *a, uint32_t y, int64_t c)
{
	Val v = { y, 0, 0 };
	if (a->vcount) {
		Var *vr = var_slot(a, y, 0);
		if (vr && vr->v.add != (uint32_t)-1) v = vr->v;
	}
	fold(a, &v);
	v.c += c;
	fold(a, &v);
	return v;
}

/* "y:" at the current position */
int as_label(As *a, uint32_t y)
{
	ElfSym *ys = elf_symp(a->o, y);
	if (ys->flags & ESYM_DEFINED) return err(a, "invalid symbol redefinition");
	elf_define(a->o, y, a->cur, elf_frag(a->o, a->cur)->size);
	return 0;
}

int as_run(As *a, ElfObj *o, const char *text, size_t len, const char *name, const char **msg)
{
	as_begin(a, o, name);
	if (as_snippet(a, text, len) || as_end(a)) { *msg = a->msg; return -1; }
	return 0;
}
