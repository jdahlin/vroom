/* Two identical integer negations or complements cancel. */
#include "../emitimpl.h"

VPRIV uint32_t em_double_unary(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n), *inner, *operand;
	uint64_t size;

	if (!e->E->o->opt || e->E->noopt || x->op != N_UNARY ||
	    (x->aux != LEX_TOKEN_MINUS && x->aux != LEX_TOKEN_TILDE))
		return 0;
	inner = N(x->a);
	if (inner->op != N_UNARY || inner->aux != x->aux)
		return 0;
	operand = N(inner->a);
	if (QUNQ(x->ty) != QUNQ(inner->ty) || QUNQ(x->ty) != QUNQ(operand->ty) ||
	    !is_integer(c, x->ty) || em_bitint(c, x->ty))
		return 0;
	size = tsize(c, x->ty);
	return size == 4 || size == 8 ? inner->a : 0;
}
