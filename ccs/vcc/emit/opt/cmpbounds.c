/* Constant results at the extrema of a converted integer comparison.
 * The caller must evaluate and discard the returned operand. */
#include "../emitimpl.h"

/* Prove a small integer constant expression pure before asking the existing
 * constant evaluator to interpret it. Unknown forms and a depleted budget
 * fall back to ordinary lowering; no calls or volatile reads are traversed. */
static bool em_bound_pure(cc *c, uint32_t n, unsigned *budget)
{
	const node *x;

	if (!n || !*budget)
		return false;
	--*budget;
	x = N(n);
	if (!is_integer(c, x->ty) || em_bitint(c, x->ty) || tsize(c, x->ty) > 8)
		return false;
	switch (x->op) {
	case N_NUM:
		return true;
	case N_VAR:
		return c->syms[x->a].kind == S_ENUMCONST;
	case N_CAST:
		return (x->aux == CK_INT || x->aux == CK_NOP || x->aux == CK_BOOL) &&
		       em_bound_pure(c, x->a, budget);
	case N_UNARY:
		return (x->aux == T_PLUS || x->aux == T_MINUS || x->aux == T_TILDE || x->aux == T_EXCLAIM) &&
		       em_bound_pure(c, x->a, budget);
	case N_BINARY:
		return (x->aux == T_PLUS || x->aux == T_MINUS || x->aux == T_STAR ||
		        x->aux == T_SLASH || x->aux == T_PERCENT || x->aux == T_LESSLESS ||
		        x->aux == T_GREATERGREATER || x->aux == T_AMP ||
		        x->aux == T_PIPE || x->aux == T_CARET) &&
		       em_bound_pure(c, x->a, budget) && em_bound_pure(c, x->b, budget);
	default:
		return false;
	}
}

static bool em_bound_constant(cc *c, uint32_t n, i128 *value)
{
	const node *x = N(n);
	unsigned budget = 16;
	cval cv;

	if (x->op == N_NUM) {
		*value = c->ipool[x->a];
		return true;
	}
	if (x->op == N_VAR && c->syms[x->a].kind == S_ENUMCONST) {
		*value = c->syms[x->a].val;
		return true;
	}
	if ((x->op != N_CAST && x->op != N_UNARY && x->op != N_BINARY) ||
	    !em_bound_pure(c, n, &budget) || !cc_eval(c, n, &cv) || cv.kind != CV_INT)
		return false;
	*value = cv.i;
	return true;
}

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
	if (QUNQ(a->ty) != QUNQ(b->ty) || !is_integer(c, a->ty) || em_bitint(c, a->ty))
		return -1;
	sz = tsize(c, a->ty);
	if (sz != 4 && sz != 8)
		return -1;
	if (em_bound_constant(c, x->b, &v)) {
		*operand = x->a;
	} else if (em_bound_constant(c, x->a, &v)) {
		*operand = x->b;
		op = op == T_LESS ? T_GREATER : op == T_LESSEQ ? T_GREATEREQ :
		     op == T_GREATER ? T_LESS : T_LESSEQ;
	} else {
		return -1;
	}
	v = em_trunc_to(c, v, a->ty);
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
