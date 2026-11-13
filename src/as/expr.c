/* expr.c: expressions, GNU as's operators and precedence, folded to a constant or a symbol plus offset, or deferred. */
#include "asimpl.h"

/* ---------------------------------------------------------------- expressions */


VPRIV int is_abs(As *a, Val *v)
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

/* v is a temporary defined by a deferred expression (elf_expr_sym) */
static int is_xsym(As *a, const Val *v)
{
	int op; ElfVal x, y;
	return v->add && !v->sub && !v->c && elf_expr_get(a->o, v->add, &op, &x, &y);
}

/* a value the layout settles: a constant, a label difference, a deferred expression */
VPRIV int deferrable(As *a, const Val *v)
{
	return is_abs(a, (Val *)v) || (v->add && v->sub) || is_xsym(a, v);
}

/* x op y as a temporary the layout defines (x and y deferrable) */
static void defer_op(As *a, int op, Val *x, const Val *y)
{
	uint32_t t = tmpsym(a);
	elf_expr_sym(a->o, t, op, *x, *y);
	*x = (Val){ t, 0, 0 };
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
	/* U, L, UL, ULL ... after a number: accepted and ignored, as MC (C headers' constants in .S) */
	{
		const char *q = p;
		while (q < e && (*q == 'u' || *q == 'U' || *q == 'l' || *q == 'L')) q++;
		if (q > p && (q >= e || !isidc((unsigned char)*q))) p = q;
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
			if (v->add && v->sub) { uint32_t t = v->add; v->add = v->sub; v->sub = t; v->c = (int64_t)(0 - (uint64_t)v->c); return 0; }
			if (is_xsym(a, v)) { Val z = { 0, 0, 0 }, y = *v; *v = z; defer_op(a, '-', v, &y); return 0; }
			if (!is_abs(a, v)) return err(a, "unsupported negation of a symbol");
			v->c = (int64_t)(0 - (uint64_t)v->c); return 0;
		}
		if (is_xsym(a, v) || (v->add && v->sub)) {	/* ~x = x ^ -1, !x = x == 0 */
			Val k = { 0, 0, c == '~' ? -1 : 0 };
			defer_op(a, c == '~' ? '^' : 'E', v, &k);
			if (c == '!') { Val one = { 0, 0, 0 }; Val m = *v; *v = one; defer_op(a, '-', v, &m); }	/* (GNU !: 1, not -1) */
			return 0;
		}
		if (!is_abs(a, v)) return err(a, "expected absolute expression");
		v->c = c == '~' ? ~v->c : !v->c;
		return 0;
	}
	if (c == '%' && a->o->machine == EM_X86_64) {
		/* a register as a value (.if \\reg == %rsp): its name's hash, the same for the same register */
		const char *p = l->p + 1, *s = p;
		while (p < l->e && (isidc((unsigned char)*p) || c_digit((unsigned char)*p))) p++;
		if (p == s) return err(a, "unknown token in expression");
		uint64_t h = 1469598103934665603ull;
		for (const char *q = s; q < p; q++) h = (h ^ (uint64_t)c_lower((unsigned char)*q)) * 1099511628211ull;
		l->p = p;
		*v = (Val){ 0, 0, (int64_t)(h >> 2) | ((int64_t)1 << 61) };
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
		if (a->nralias) {
			const char *rn = as_ralias(a, y);
			if (rn) {	/* an alias of a register: as the register */
				uint64_t h = 1469598103934665603ull;
				for (const char *q = rn; *q; q++) h = (h ^ (uint64_t)c_lower((unsigned char)*q)) * 1099511628211ull;
				*v = (Val){ 0, 0, (int64_t)(h >> 2) | ((int64_t)1 << 61) };
				return 0;
			}
		}
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
		if ((is_xsym(a, x) || is_xsym(a, y)) && deferrable(a, x) && deferrable(a, y)) {
			defer_op(a, op, x, y);
			return 0;
		}
		if (op == '-') { r.add = y->sub; r.sub = y->add; r.c = -y->c; }
		if ((x->add && r.add) || (x->sub && r.sub)) {
			/* try to cancel: x.add - r.sub etc. handled by fold below */
			Val t = { x->add, r.sub, 0 }, u = { r.add, x->sub, 0 };
			fold(a, &t); fold(a, &u);
			if (t.add && t.sub && u.add && u.sub) {
				if (deferrable(a, x) && deferrable(a, y)) { defer_op(a, op, x, y); return 0; }	/* (a-b) - (c-d) across frags */
				return err(a, "expression too complex for a relocation");
			}
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
		int diff_x = deferrable(a, x), diff_y = deferrable(a, y);
		if (!diff_x || !diff_y || !strchr("*/%LR&|^!AOEN<>lg", op)) return err(a, "expected absolute expression");
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
