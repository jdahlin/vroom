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
		return (x->aux == LEX_TOKEN_PLUS || x->aux == LEX_TOKEN_MINUS || x->aux == LEX_TOKEN_TILDE || x->aux == LEX_TOKEN_EXCLAMATION) &&
		       em_bound_pure(c, x->a, budget);
	case N_BINARY:
		return (x->aux == LEX_TOKEN_PLUS || x->aux == LEX_TOKEN_MINUS || x->aux == LEX_TOKEN_STAR ||
		        x->aux == LEX_TOKEN_SLASH || x->aux == LEX_TOKEN_PERCENT || x->aux == LEX_TOKEN_LESS_LESS ||
		        x->aux == LEX_TOKEN_GREATER_GREATER || x->aux == LEX_TOKEN_AMPERSAND ||
		        x->aux == LEX_TOKEN_PIPE || x->aux == LEX_TOKEN_CARET) &&
		       em_bound_pure(c, x->a, budget) && em_bound_pure(c, x->b, budget);
	default:
		return false;
	}
}

VPRIV bool em_bound_constant(cc *c, uint32_t n, i128 *value)
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
	if (op != LEX_TOKEN_LESS && op != LEX_TOKEN_LESS_EQUAL && op != LEX_TOKEN_GREATER && op != LEX_TOKEN_GREATER_EQUAL)
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
		op = op == LEX_TOKEN_LESS ? LEX_TOKEN_GREATER : op == LEX_TOKEN_LESS_EQUAL ? LEX_TOKEN_GREATER_EQUAL :
		     op == LEX_TOKEN_GREATER ? LEX_TOKEN_LESS : LEX_TOKEN_LESS_EQUAL;
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
	if (v == min && (op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER_EQUAL))
		return op == LEX_TOKEN_GREATER_EQUAL;
	if (v == max && (op == LEX_TOKEN_GREATER || op == LEX_TOKEN_LESS_EQUAL))
		return op == LEX_TOKEN_LESS_EQUAL;
	return -1;
}
