/*
 * condition-fold.c: decide a condition at compile time from what the walker knows: ranges of integer
 * variables, repeated pure operands, comparisons of a value with itself, floating-point facts.
 *
 * codegen_condition_fold(e, n) returns 1 or 0 when the condition n is that constant, -1 when unknown.  It must
 * only answer when it is certain: the caller drops the code on the dead side, so a wrong answer
 * silently deletes live code.  Only pure expressions are examined (no calls, volatile reads or
 * atomics: codegen_expression_is_same_pure, irange), within the limits of the types (trange).  Called by the shared walker
 * (walk/expression.c) and by arm64's direct emitter (arch/a64/codegen/); it reads the unit's
 * floating-point facts (E->ffacts) but changes nothing.
 */
#include "codegen-internal.h"

/* jump to l if n's truth == jump_if; returns the constant truth or -1 */
/* e >> 0, e << 0: e */
static uint32_t codegen_expression_shift_zero_strip(cc *c, uint32_t n)
{
	while (N(n)->op == AST_NODE_BINARY && (N(n)->aux == LEX_TOKEN_LESS_LESS || N(n)->aux == LEX_TOKEN_GREATER_GREATER) &&
	       N(N(n)->b)->op == AST_NODE_NUMBER && c->ipool[N(N(n)->b)->a] == 0 && AST_QUALIFIED_TYPE_UNQUALIFIED(N(n)->ty) == AST_QUALIFIED_TYPE_UNQUALIFIED(N(N(n)->a)->ty))
		n = N(n)->a;
	return n;
}

/* the same value, without side effects (integer or pointer, nothing volatile) */
static bool codegen_expression_is_same_pure(cc *c, uint32_t a, uint32_t b)
{
	const node *x, *y;

	a = codegen_expression_shift_zero_strip(c, a), b = codegen_expression_shift_zero_strip(c, b);
	x = N(a), y = N(b);
	if (x->op != y->op || AST_QUALIFIED_TYPE_UNQUALIFIED(x->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(y->ty) || (AST_QUALIFIED_TYPE_QUALIFIERS(x->ty) | AST_QUALIFIED_TYPE_QUALIFIERS(y->ty)) & AST_QUALIFIER_VOLATILE ||
	    !(codegen_type_is_integer(c, x->ty) || codegen_type_is_pointer(c, x->ty)))
		return false;
	switch (x->op) {
	case AST_NODE_VARIABLE:
		return x->a == y->a && c->syms[x->a].kind == AST_SYMBOL_VARIABLE && !(AST_QUALIFIED_TYPE_QUALIFIERS(c->syms[x->a].ty) & AST_QUALIFIER_VOLATILE);
	case AST_NODE_NUMBER:
		return c->ipool[x->a] == c->ipool[y->a];
	case AST_NODE_CAST:
		return x->aux == y->aux && codegen_expression_is_same_pure(c, x->a, y->a);
	case AST_NODE_BINARY:
		return x->aux == y->aux && !codegen_operator_is_comparison(x->aux) && x->aux != LEX_TOKEN_SLASH && x->aux != LEX_TOKEN_PERCENT &&
		       codegen_expression_is_same_pure(c, x->a, y->a) && codegen_expression_is_same_pure(c, x->b, y->b);
	}
	return false;
}

/* a comparison of two integers as the outcomes it holds for (1 <, 2 ==, 4 >) and its operands */
static int codegen_comparison_outcomes(cc *c, uint32_t n, uint32_t *l, uint32_t *r)
{
	const node *x = N(n);
	int m;

	if (x->op != AST_NODE_BINARY || !codegen_operator_is_comparison(x->aux) || codegen_type_is_float(c, N(x->a)->ty) || codegen_type_is_wide(c, N(x->a)->ty))
		return 0;
	switch (x->aux) {
	case LEX_TOKEN_LESS: m = 1; break;
	case LEX_TOKEN_EQUAL_EQUAL: m = 2; break;
	case LEX_TOKEN_LESS_EQUAL: m = 3; break;
	case LEX_TOKEN_GREATER: m = 4; break;
	case LEX_TOKEN_EXCLAMATION_EQUAL: m = 5; break;
	default: m = 6; break;
	}
	*l = x->a, *r = x->b;
	return m;
}

/* the values of an integer type */
static bool codegen_type_range(cc *c, ast_qualified_type t, i128 *lo, i128 *hi)
{
	uint64_t sz;

	if (!codegen_type_is_integer(c, t) || codegen_type_is_wide(c, t) || KIND(c, t) == AST_TYPE_BOOL)
		return false;
	sz = codegen_type_size(c, t) * 8;
	if (codegen_type_is_signed(c, t))
		*lo = -((i128)1 << (sz - 1)), *hi = ((i128)1 << (sz - 1)) - 1;
	else
		*lo = 0, *hi = ((i128)1 << sz) - 1;
	return true;
}

/* an interval holding every value of pure integer expression n, with variable s in [slo, shi] */
static bool codegen_expression_range(cc *c, uint32_t n, uint32_t s, i128 slo, i128 shi, i128 *lo, i128 *hi)
{
	const node *x = N(n);
	i128 tlo, thi, alo, ahi, blo, bhi;

	cc_value cv;

	if (!codegen_type_range(c, x->ty, &tlo, &thi) || (AST_QUALIFIED_TYPE_QUALIFIERS(x->ty) & AST_QUALIFIER_VOLATILE))
		return false;
	if (x->op != AST_NODE_VARIABLE && cc_eval(c, n, &cv) && cv.kind == CC_VALUE_INTEGER) {
		*lo = *hi = codegen_integer_truncate(c, cv.i, x->ty);
		return true;
	}
	switch (x->op) {
	case AST_NODE_NUMBER:
		*lo = *hi = codegen_integer_truncate(c, c->ipool[x->a], x->ty);
		return true;
	case AST_NODE_VARIABLE:
		if (x->a == s)
			*lo = slo, *hi = shi;
		else
			*lo = tlo, *hi = thi;
		return true;
	case AST_NODE_CAST:
		if (!codegen_expression_range(c, x->a, s, slo, shi, &alo, &ahi))
			return false;
		break;
	case AST_NODE_BINARY:
		if ((x->aux != LEX_TOKEN_PLUS && x->aux != LEX_TOKEN_MINUS) || !codegen_expression_range(c, x->a, s, slo, shi, &alo, &ahi) ||
		    !codegen_expression_range(c, x->b, s, slo, shi, &blo, &bhi))
			return false;
		if (x->aux == LEX_TOKEN_PLUS)
			alo += blo, ahi += bhi;
		else
			alo -= bhi, ahi -= blo;
		break;
	default:
		return false;
	}
	/* out of the type's range: anything (wraps) */
	if (alo < tlo || ahi > thi)
		alo = tlo, ahi = thi;
	*lo = alo, *hi = ahi;
	return true;
}

/* comparison n with s in [slo, shi]: 0 or 1 when the intervals decide it, -1 */
static int codegen_expression_range_compare(cc *c, uint32_t n, uint32_t s, i128 slo, i128 shi)
{
	const node *x = N(n);
	i128 alo, ahi, blo, bhi;

	if (x->op != AST_NODE_BINARY || !codegen_operator_is_comparison(x->aux) || !codegen_expression_range(c, x->a, s, slo, shi, &alo, &ahi) ||
	    !codegen_expression_range(c, x->b, s, slo, shi, &blo, &bhi))
		return -1;
	switch (x->aux) {
	case LEX_TOKEN_LESS: return ahi < blo ? 1 : alo >= bhi ? 0 : -1;
	case LEX_TOKEN_LESS_EQUAL: return ahi <= blo ? 1 : alo > bhi ? 0 : -1;
	case LEX_TOKEN_GREATER: return alo > bhi ? 1 : ahi <= blo ? 0 : -1;
	case LEX_TOKEN_GREATER_EQUAL: return alo >= bhi ? 1 : ahi < blo ? 0 : -1;
	case LEX_TOKEN_EQUAL_EQUAL: return alo == ahi && blo == bhi && alo == blo ? 1 : ahi < blo || bhi < alo ? 0 : -1;
	case LEX_TOKEN_EXCLAMATION_EQUAL: return alo == ahi && blo == bhi && alo == blo ? 0 : ahi < blo || bhi < alo ? 1 : -1;
	}
	return -1;
}

/* the range of local s where comparison n (s OP constant) has truth t */
static bool codegen_comparison_variable_range(cc *c, uint32_t n, bool t, uint32_t *s, i128 *lo, i128 *hi)
{
	const node *x = N(n);
	int op;
	i128 k;

	if (x->op != AST_NODE_BINARY || !codegen_operator_is_comparison(x->aux) || N(x->a)->op != AST_NODE_VARIABLE || N(x->b)->op != AST_NODE_NUMBER ||
	    !codegen_type_range(c, N(x->a)->ty, lo, hi) || (AST_QUALIFIED_TYPE_QUALIFIERS(N(x->a)->ty) & AST_QUALIFIER_VOLATILE) || c->syms[N(x->a)->a].kind != AST_SYMBOL_VARIABLE)
		return false;
	op = x->aux, k = codegen_integer_truncate(c, c->ipool[N(x->b)->a], N(x->a)->ty);
	if (!t)
		op = op == LEX_TOKEN_LESS ? LEX_TOKEN_GREATER_EQUAL : op == LEX_TOKEN_LESS_EQUAL ? LEX_TOKEN_GREATER : op == LEX_TOKEN_GREATER ? LEX_TOKEN_LESS_EQUAL :
		     op == LEX_TOKEN_GREATER_EQUAL ? LEX_TOKEN_LESS : op == LEX_TOKEN_EQUAL_EQUAL ? LEX_TOKEN_EXCLAMATION_EQUAL : LEX_TOKEN_EQUAL_EQUAL;
	*s = N(x->a)->a;
	switch (op) {
	case LEX_TOKEN_LESS: *hi = k - 1; break;
	case LEX_TOKEN_LESS_EQUAL: *hi = k; break;
	case LEX_TOKEN_GREATER: *lo = k + 1; break;
	case LEX_TOKEN_GREATER_EQUAL: *lo = k; break;
	case LEX_TOKEN_EQUAL_EQUAL: *lo = *hi = k; break;
	default: return false;
	}
	return *lo <= *hi;
}

/* a condition known from its form (x != x, x < y && x > y, ...): 0 or 1; -1 unknown */
/* a floating local whose every write has fact f (CODEGEN_FLOAT_FACT_NONNEGATIVE / CODEGEN_FLOAT_FACT_ZERO), or a +-0.0 constant for CODEGEN_FLOAT_FACT_ZERO */
static bool codegen_float_local_has_fact(codegen_context *e, uint32_t n, uint8_t f)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	cc_value cv;

	if (x->op == AST_NODE_VARIABLE && codegen_symbol_is_local_variable(c, x->a) && !(AST_QUALIFIED_TYPE_QUALIFIERS(x->ty) & AST_QUALIFIER_VOLATILE)) {
		uint8_t g = e->unit->float_facts[x->a];

		return (g & (CODEGEN_FLOAT_FACT_SEEN | CODEGEN_FLOAT_FACT_BAD)) == CODEGEN_FLOAT_FACT_SEEN && (g & f) == f;
	}
	return f == CODEGEN_FLOAT_FACT_ZERO && codegen_type_is_float(c, x->ty) && cc_eval(c, n, &cv) && cv.kind == CC_VALUE_FLOAT && (cv.ld ? !(cv.q << 1) : cv.f == 0);
}

VPRIV int codegen_condition_fold(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	uint32_t l1, r1, l2, r2, s;
	int m1, m2, m;
	i128 lo, hi;

	/* a || b with b true wherever a is false (a && b: b false wherever a is true) */
	if ((x->op == AST_NODE_LOGICAL_AND || x->op == AST_NODE_LOGICAL_OR) && codegen_comparison_variable_range(c, x->a, x->op == AST_NODE_LOGICAL_AND, &s, &lo, &hi)) {
		int k = codegen_expression_range_compare(c, x->b, s, lo, hi);

		if (x->op == AST_NODE_LOGICAL_OR && k == 1)
			return 1;
		if (x->op == AST_NODE_LOGICAL_AND && k == 0)
			return 0;
	}
	if (x->op == AST_NODE_UNARY && x->aux == LEX_TOKEN_EXCLAMATION) {
		int k = codegen_condition_fold(e, x->a);

		return k < 0 ? -1 : !k;
	}
	if (x->op == AST_NODE_LOGICAL_AND || x->op == AST_NODE_LOGICAL_OR) {
		m1 = codegen_comparison_outcomes(c, x->a, &l1, &r1);
		m2 = codegen_comparison_outcomes(c, x->b, &l2, &r2);
		if (!m1 || !m2)
			return -1;
		if (codegen_expression_is_same_pure(c, l1, r2) && codegen_expression_is_same_pure(c, r1, l2))	/* swapped: < and > trade places */
			m2 = (m2 & 2) | (m2 & 1) << 2 | (m2 & 4) >> 2;
		else if (!codegen_expression_is_same_pure(c, l1, l2) || !codegen_expression_is_same_pure(c, r1, r2))
			return -1;
		m = x->op == AST_NODE_LOGICAL_AND ? m1 & m2 : m1 | m2;
		return m == 0 ? 0 : m == 7 ? 1 : -1;
	}
	/* fabs(x) < 0.0 (through locals): false, NaN included */
	if (x->op == AST_NODE_BINARY && (x->aux == LEX_TOKEN_LESS || x->aux == LEX_TOKEN_GREATER) && codegen_type_is_float(c, N(x->a)->ty) &&
	    !codegen_type_is_long_double(c, N(x->a)->ty) && codegen_float_local_has_fact(e, x->aux == LEX_TOKEN_LESS ? x->a : x->b, CODEGEN_FLOAT_FACT_NONNEGATIVE) &&
	    codegen_float_local_has_fact(e, x->aux == LEX_TOKEN_LESS ? x->b : x->a, CODEGEN_FLOAT_FACT_ZERO))
		return 0;
	m = codegen_comparison_outcomes(c, n, &l1, &r1);
	if (m && codegen_expression_is_same_pure(c, l1, r1))
		return (m & 2) != 0;
	return -1;
}

VPRIV bool codegen_operator_is_comparison(int op)
{
	return op == LEX_TOKEN_LESS || op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER || op == LEX_TOKEN_GREATER_EQUAL || op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL;
}
