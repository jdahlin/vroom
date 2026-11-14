/*
 * cmpzero.c: recognise comparisons that are true exactly when an integer is zero (x == 0,
 * x <= 0 for unsigned x, 0 >= x), so the backend can use its compare-with-zero form.
 *
 * em_cmpzero(e, n, &operand) sets operand to the other side and returns true.  Only 4- and 8-byte
 * integers of one type, only at -O1 and up.  Only wasm's emitter calls it today; it uses
 * em_bound_constant from cmpbounds.c to see the constant 0.
 */
#include "../codegen-internal.h"

VPRIV bool em_cmpzero(codegen_context *e, uint32_t n, uint32_t *operand)
{
	cc *c = e->compile_context;
	const node *x = N(n), *a, *b;
	int op;
	uint64_t size;
	i128 value;

	if (!e->unit->options->opt || e->unit->cleanups_disabled || x->op != AST_NODE_BINARY)
		return false;
	op = x->aux;
	if (op != LEX_TOKEN_EQUAL_EQUAL && op != LEX_TOKEN_LESS_EQUAL && op != LEX_TOKEN_GREATER_EQUAL)
		return false;
	a = N(x->a); b = N(x->b);
	if (AST_QUALIFIED_TYPE_UNQUALIFIED(a->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(b->ty) || !codegen_type_is_integer(c, a->ty) || codegen_type_is_bitint(c, a->ty))
		return false;
	size = codegen_type_size(c, a->ty);
	if (size != 4 && size != 8)
		return false;
	if (em_bound_constant(c, x->b, &value) && codegen_integer_truncate(c, value, b->ty) == 0 &&
	    (op == LEX_TOKEN_EQUAL_EQUAL || (op == LEX_TOKEN_LESS_EQUAL && !codegen_type_is_signed(c, a->ty)))) {
		*operand = x->a;
		return true;
	}
	if (em_bound_constant(c, x->a, &value) && codegen_integer_truncate(c, value, a->ty) == 0 &&
	    (op == LEX_TOKEN_EQUAL_EQUAL || (op == LEX_TOKEN_GREATER_EQUAL && !codegen_type_is_signed(c, a->ty)))) {
		*operand = x->b;
		return true;
	}
	return false;
}
