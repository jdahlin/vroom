/* vcc code generator: conditions and range folding */
#include "../../../codegen/emitimpl.h"

/* ---------------- conditions ---------------- */

VPRIV void gen_cond(ecx *e, uint32_t n, bool jump_if, uint32_t l)
{
	cc *c = e->c;
	const node *x = N(n);
	int k = e->E->o->opt && !e->E->noopt ? em_const_truth(e, n, a64_constant_p) : -1;
	if (k < 0) k = cond_fold(e, n);

	if (k >= 0) {
		if (k == (int)jump_if)
			jump(e, l);
		return;
	}
	switch (x->op) {
	case AST_NODE_LOGICAL_AND:
	case AST_NODE_LOGICAL_OR: {
		bool isand = x->op == AST_NODE_LOGICAL_AND;

		if (isand != jump_if) {
			/* && jumping on false / || jumping on true: both operands jump to l */
			gen_cond(e, x->a, jump_if, l);
			gen_cond(e, x->b, jump_if, l);
		} else {
			uint32_t skip = newlabel(e);

			gen_cond(e, x->a, !jump_if, skip);
			gen_cond(e, x->b, jump_if, l);
			deflabel(e, skip);
		}
		return;
	}
	case AST_NODE_UNARY:
		if (x->aux == LEX_TOKEN_EXCLAMATION) {
			gen_cond(e, x->a, !jump_if, l);
			return;
		}
		break;
	case AST_NODE_CAST:
		if (x->aux == AST_CAST_BOOL) {
			gen_cond(e, x->a, jump_if, l);
			return;
		}
		break;
	case AST_NODE_BINARY:
		if (em_is_cmp(x->aux) && !is_wide(c, N(x->a)->ty) && !is_flt(c, N(x->a)->ty)) {
			val v = gen(e, n);

			if (v.k == V_CONST || v.k == V_ADDR) {
				test_zero(e, &v, x->ty, l, jump_if);
				return;
			}
			/* gen left a cset: turn it back into a branch on the flags */
			if (e->reach && e->nin && e->in[e->nin - 1].op == I_CSINC && e->in[e->nin - 1].rn == ZR &&
			    e->in[e->nin - 1].rm == ZR && e->in[e->nin - 1].rd == v.r) {
				int cond = e->in[e->nin - 1].cond ^ 1;

				e->nin--;
				bcond(e, jump_if ? cond : cond ^ 1, l);
				val_free(e, &v);
				return;
			}
			test_zero(e, &v, x->ty, l, jump_if);
			return;
		}
		break;
	}
	{
		val v = gen(e, n);

		test_zero(e, &v, x->ty, l, jump_if);
	}
}
