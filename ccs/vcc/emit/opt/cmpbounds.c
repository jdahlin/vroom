/* Constant results at the extrema of a converted integer comparison.
 * The caller must evaluate and discard the returned operand. */
#include "../emitimpl.h"

VPRIV int em_cmpbounds(ecx *e, uint32_t n, uint32_t *operand)
{
	cc *c = e->c;
	const node *x = N(n), *a, *b;
	int op;
	i128 v, min, max;
	uint64_t sz;

	if (!e->E->o->opt || e->E->noopt || x->op != N_BINARY)
		return -1;
	op = x->aux;
	if (op != T_LESS && op != T_LESSEQ && op != T_GREATER && op != T_GREATEREQ)
		return -1;
	a = N(x->a); b = N(x->b);
	if (a->op == N_NUM && b->op != N_NUM) {
		const node *tmp = a; a = b; b = tmp;
		*operand = x->b;
		op = op == T_LESS ? T_GREATER : op == T_LESSEQ ? T_GREATEREQ :
		     op == T_GREATER ? T_LESS : T_LESSEQ;
	} else {
		*operand = x->a;
	}
	if (b->op != N_NUM || QUNQ(a->ty) != QUNQ(b->ty) ||
	    !is_integer(c, a->ty) || em_bitint(c, a->ty))
		return -1;
	sz = tsize(c, a->ty);
	if (sz != 4 && sz != 8)
		return -1;
	v = em_trunc_to(c, c->ipool[b->a], a->ty);
	if (is_signed(c, a->ty)) {
		min = -((i128)1 << (sz * 8 - 1));
		max = ((i128)1 << (sz * 8 - 1)) - 1;
	} else {
		min = 0;
		max = (i128)(((u128)1 << (sz * 8)) - 1);
	}
	if (v == min && (op == T_LESS || op == T_GREATEREQ))
		return op == T_GREATEREQ;
	if (v == max && (op == T_GREATER || op == T_LESSEQ))
		return op == T_LESSEQ;
	return -1;
}
