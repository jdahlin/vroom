/* expression.c: expressions, GNU as's operators and precedence, folded to a constant or a symbol plus offset, or deferred. */
#include "as-internal.h"

/* ---------------------------------------------------------------- expressions */


VPRIV int as_value_is_absolute(as_context *a, as_value *v)
{
	(void)a;
	return !v->add && !v->sub;
}

/* fold add - sub when both are in the same frag (or absolute) */
VPRIV void as_value_fold(as_context *a, as_value *v)
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
static int as_value_is_deferred_symbol(as_context *a, const as_value *v)
{
	int op; ElfVal x, y;
	return v->add && !v->sub && !v->c && elf_expr_get(a->o, v->add, &op, &x, &y);
}

/* a value the layout settles: a constant, a label difference, a deferred expression */
VPRIV int as_value_is_deferrable(as_context *a, const as_value *v)
{
	return as_value_is_absolute(a, (as_value *)v) || (v->add && v->sub) || as_value_is_deferred_symbol(a, v);
}

/* x op y as a temporary the layout defines (x and y deferrable) */
static void as_value_defer_operation(as_context *a, int op, as_value *x, const as_value *y)
{
	uint32_t t = as_symbol_temporary(a);
	elf_expr_sym(a->o, t, op, *x, *y);
	*x = (as_value){ t, 0, 0 };
}

typedef struct { as_context *a; const char *p, *e; } as_expression_reader;

static int as_expression_parse(as_expression_reader *l, as_value *v);

static int as_number_parse(as_expression_reader *l, as_value *v)
{
	const char *p = l->p, *e = l->e;
	uint64_t x = 0;
	if (p + 1 < e && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		p += 2;
		const char *s = p;
		while (p < e && as_char_is_hex_digit((unsigned char)*p)) { x = x * 16 + (uint64_t)(as_char_is_digit((unsigned char)*p) ? *p - '0' : (as_char_to_lower((unsigned char)*p) - 'a' + 10)); p++; }
		if (p == s) return as_error_report(l->a, "invalid hexadecimal number");
	} else if (p + 2 < e && p[0] == '0' && (p[1] == 'b' || p[1] == 'B') && (p[2] == '0' || p[2] == '1')) {
		p += 2;
		while (p < e && (*p == '0' || *p == '1')) x = x * 2 + (uint64_t)(*p++ - '0');
	} else {
		const char *s = p;
		while (p < e && as_char_is_digit((unsigned char)*p)) p++;
		/* local label reference */
		if (p < e && (*p == 'b' || *p == 'f') && (p + 1 >= e || !as_char_is_identifier((unsigned char)p[1]))) {
			uint32_t num = 0;
			for (const char *q = s; q < p; q++) num = num * 10 + (uint32_t)(*q - '0');
			as_numeric_label *nl = as_numeric_label_find(l->a, num);
			uint32_t y;
			if (*p == 'b') {
				if (!nl->cur) return as_error_report(l->a, "directional label undefined");
				y = nl->cur;
			} else {
				if (!nl->fwd) nl->fwd = as_symbol_temporary(l->a);
				y = nl->fwd;
			}
			l->p = p + 1;
			*v = (as_value){ y, 0, 0 };
			as_value_fold(l->a, v);
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
		if (q > p && (q >= e || !as_char_is_identifier((unsigned char)*q))) p = q;
	}
	l->p = p;
	*v = (as_value){ 0, 0, (int64_t)x };
	return 0;
}

static int as_expression_primary_parse(as_expression_reader *l, as_value *v)
{
	as_context *a = l->a;
	l->p = as_whitespace_skip(l->p, l->e);
	if (l->p >= l->e) return as_error_report(a, "unexpected end of expression");
	char c = *l->p;
	if (c == '(') {
		l->p++;
		if (as_expression_parse(l, v)) return -1;
		l->p = as_whitespace_skip(l->p, l->e);
		if (l->p >= l->e || *l->p != ')') return as_error_report(a, "expected ')' in expression");
		l->p++;
		return 0;
	}
	if (c == '-' || c == '~' || c == '!' || c == '+') {
		l->p++;
		if (as_expression_primary_parse(l, v)) return -1;
		if (c == '+') return 0;
		if (c == '-') {
			if (v->add && v->sub) { uint32_t t = v->add; v->add = v->sub; v->sub = t; v->c = (int64_t)(0 - (uint64_t)v->c); return 0; }
			if (as_value_is_deferred_symbol(a, v)) { as_value z = { 0, 0, 0 }, y = *v; *v = z; as_value_defer_operation(a, '-', v, &y); return 0; }
			if (!as_value_is_absolute(a, v)) return as_error_report(a, "unsupported negation of a symbol");
			v->c = (int64_t)(0 - (uint64_t)v->c); return 0;
		}
		if (as_value_is_deferred_symbol(a, v) || (v->add && v->sub)) {	/* ~x = x ^ -1, !x = x == 0 */
			as_value k = { 0, 0, c == '~' ? -1 : 0 };
			as_value_defer_operation(a, c == '~' ? '^' : 'E', v, &k);
			if (c == '!') { as_value one = { 0, 0, 0 }; as_value m = *v; *v = one; as_value_defer_operation(a, '-', v, &m); }	/* (GNU !: 1, not -1) */
			return 0;
		}
		if (!as_value_is_absolute(a, v)) return as_error_report(a, "expected absolute expression");
		v->c = c == '~' ? ~v->c : !v->c;
		return 0;
	}
	if (c == '%' && a->o->machine == EM_X86_64) {
		/* a register as a value (.if \\reg == %rsp): its name's hash, the same for the same register */
		const char *p = l->p + 1, *s = p;
		while (p < l->e && (as_char_is_identifier((unsigned char)*p) || as_char_is_digit((unsigned char)*p))) p++;
		if (p == s) return as_error_report(a, "unknown token in expression");
		uint64_t h = 1469598103934665603ull;
		for (const char *q = s; q < p; q++) h = (h ^ (uint64_t)as_char_to_lower((unsigned char)*q)) * 1099511628211ull;
		l->p = p;
		*v = (as_value){ 0, 0, (int64_t)(h >> 2) | ((int64_t)1 << 61) };
		return 0;
	}
	if (as_char_is_digit((unsigned char)c)) return as_number_parse(l, v);
	if (c == '\'') {
		const char *p = l->p + 1;
		if (p >= l->e) return as_error_report(a, "bad character literal");
		int64_t x;
		if (*p == '\\' && p + 1 < l->e) {
			p++;
			switch (*p) { case 'n': x = '\n'; break; case 't': x = '\t'; break; case '0': x = 0; break; default: x = (unsigned char)*p; }
		} else x = (unsigned char)*p;
		p++;
		if (p < l->e && *p == '\'') p++;
		l->p = p;
		*v = (as_value){ 0, 0, x };
		return 0;
	}
	if (c == '.' && (l->p + 1 >= l->e || !as_char_is_identifier((unsigned char)l->p[1]))) {
		l->p++;
		*v = (as_value){ as_label_temporary_define(a), 0, 0 };
		return 0;
	}
	if (as_char_is_identifier_start((unsigned char)c) || c == '"') {
		const char *s, *p;
		if (c == '"') {
			s = l->p + 1; p = s;
			while (p < l->e && *p != '"') p++;
			if (p >= l->e) return as_error_report(a, "unterminated quoted symbol");
			l->p = p + 1;
		} else {
			s = l->p; p = s;
			while (p < l->e && as_char_is_identifier((unsigned char)*p)) p++;
			l->p = p;
		}
		uint32_t y = as_symbol_intern(a, s, (size_t)(p - s));
		*v = (as_value){ y, 0, 0 };
		if (a->register_alias_count) {
			const char *rn = as_register_alias_find(a, y);
			if (rn) {	/* an alias of a register: as the register */
				uint64_t h = 1469598103934665603ull;
				for (const char *q = rn; *q; q++) h = (h ^ (uint64_t)as_char_to_lower((unsigned char)*q)) * 1099511628211ull;
				*v = (as_value){ 0, 0, (int64_t)(h >> 2) | ((int64_t)1 << 61) };
				return 0;
			}
		}
		if (a->variable_count) {
			as_variable *vr = as_variable_slot_find(a, y, 0);
			if (vr && vr->v.add != (uint32_t)-1) *v = vr->v;
		}
		as_value_fold(a, v);
		return 0;
	}
	return as_error_report(a, "unknown token in expression");
}

/* GNU precedences as MC uses them */
static int as_operator_parse(as_expression_reader *l, int *op)
{
	const char *p = as_whitespace_skip(l->p, l->e), *e = l->e;
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

static int as_operator_apply(as_context *a, int op, as_value *x, as_value *y)
{
	if (op == '+' || op == '-') {
		as_value r = *y;
		if ((as_value_is_deferred_symbol(a, x) || as_value_is_deferred_symbol(a, y)) && as_value_is_deferrable(a, x) && as_value_is_deferrable(a, y)) {
			as_value_defer_operation(a, op, x, y);
			return 0;
		}
		if (op == '-') { r.add = y->sub; r.sub = y->add; r.c = -y->c; }
		if ((x->add && r.add) || (x->sub && r.sub)) {
			/* try to cancel: x.add - r.sub etc. handled by fold below */
			as_value t = { x->add, r.sub, 0 }, u = { r.add, x->sub, 0 };
			as_value_fold(a, &t); as_value_fold(a, &u);
			if (t.add && t.sub && u.add && u.sub) {
				if (as_value_is_deferrable(a, x) && as_value_is_deferrable(a, y)) { as_value_defer_operation(a, op, x, y); return 0; }	/* (a-b) - (c-d) across frags */
				return as_error_report(a, "expression too complex for a relocation");
			}
			as_value res = { 0, 0, x->c + r.c + t.c + u.c };
			if (t.add) res.add = t.add; if (t.sub) res.sub = t.sub;
			if (u.add) { if (res.add) return as_error_report(a, "expression too complex"); res.add = u.add; }
			if (u.sub) { if (res.sub) return as_error_report(a, "expression too complex"); res.sub = u.sub; }
			*x = res;
			as_value_fold(a, x);
			return 0;
		}
		x->add = x->add ? x->add : r.add;
		x->sub = x->sub ? x->sub : r.sub;
		x->c += r.c;
		as_value_fold(a, x);
		return 0;
	}
	if (!as_value_is_absolute(a, x) || !as_value_is_absolute(a, y)) {
		/* (label difference) op constant, e.g. (end - start) >> 5 with end not yet placed:
		   a temporary symbol that gets its value once the layout is known */
		int diff_x = as_value_is_deferrable(a, x), diff_y = as_value_is_deferrable(a, y);
		if (!diff_x || !diff_y || !strchr("*/%LR&|^!AOEN<>lg", op)) return as_error_report(a, "expected absolute expression");
		uint32_t t = as_symbol_temporary(a);
		elf_expr_sym(a->o, t, op, *x, *y);
		*x = (as_value){ t, 0, 0 };
		return 0;
	}
	int64_t p = x->c, q = y->c, r;
	switch (op) {
	case '*': r = (int64_t)((uint64_t)p * (uint64_t)q); break;
	case '/': if (!q) return as_error_report(a, "division by zero"); r = p / q; break;
	case '%': if (!q) return as_error_report(a, "division by zero"); r = p % q; break;
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
	default: return as_error_report(a, "bad operator");
	}
	x->c = r;
	return 0;
}

static int as_expression_precedence_parse(as_expression_reader *l, as_value *v, int minp)
{
	if (as_expression_primary_parse(l, v)) return -1;
	for (;;) {
		const char *save = l->p;
		int op, p = as_operator_parse(l, &op);
		if (!p || p < minp) { l->p = save; return 0; }
		as_value r;
		if (as_expression_precedence_parse(l, &r, p + 1)) return -1;
		if (as_operator_apply(l->a, op, v, &r)) return -1;
	}
}

static int as_expression_parse(as_expression_reader *l, as_value *v) { return as_expression_precedence_parse(l, v, 1); }

VPRIV int as_expression_absolute_parse(as_context *a, const char **pp, const char *e, int64_t *out)
{
	as_expression_reader l = { a, *pp, e };
	as_value v;
	if (as_expression_parse(&l, &v)) return -1;
	*pp = l.p;
	if (!as_value_is_absolute(a, &v)) return as_error_report(a, "expected absolute expression");
	*out = v.c;
	return 0;
}

VPRIV int as_expression_value_parse(as_context *a, const char **pp, const char *e, as_value *v)
{
	as_expression_reader l = { a, *pp, e };
	if (as_expression_parse(&l, v)) return -1;
	*pp = l.p;
	return 0;
}
