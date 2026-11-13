/* Recognize comparisons that are true precisely when an integer is zero. */
#include "../emitimpl.h"

VPRIV bool em_cmpzero(ecx *e, uint32_t n, uint32_t *operand)
{
	cc *c = e->c;
	const node *x = N(n), *a, *b;
	int op;
	uint64_t size;
	i128 value;

	if (!e->E->o->opt || e->E->noopt || x->op != N_BINARY)
		return false;
	op = x->aux;
	if (op != LEX_TOKEN_EQUAL_EQUAL && op != LEX_TOKEN_LESS_EQUAL && op != LEX_TOKEN_GREATER_EQUAL)
		return false;
	a = N(x->a); b = N(x->b);
	if (QUNQ(a->ty) != QUNQ(b->ty) || !is_integer(c, a->ty) || em_bitint(c, a->ty))
		return false;
	size = tsize(c, a->ty);
	if (size != 4 && size != 8)
		return false;
	if (em_bound_constant(c, x->b, &value) && em_trunc_to(c, value, b->ty) == 0 &&
	    (op == LEX_TOKEN_EQUAL_EQUAL || (op == LEX_TOKEN_LESS_EQUAL && !is_signed(c, a->ty)))) {
		*operand = x->a;
		return true;
	}
	if (em_bound_constant(c, x->a, &value) && em_trunc_to(c, value, a->ty) == 0 &&
	    (op == LEX_TOKEN_EQUAL_EQUAL || (op == LEX_TOKEN_GREATER_EQUAL && !is_signed(c, a->ty)))) {
		*operand = x->b;
		return true;
	}
	return false;
}
