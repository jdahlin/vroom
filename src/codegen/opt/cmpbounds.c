/*
 * cmpbounds.c: constant results of an integer comparison against the extreme value of its type
 * (x < INT_MIN is 0, x <= INT_MAX is 1 ...).
 *
 * em_cmpbounds(e, n, &operand) returns 0 or 1 when the comparison n is constant, else -1; operand
 * is the non-constant side, which the caller must still evaluate and discard (it may have effects).
 * Only 4- and 8-byte integers, and only at -O1 and up (cc_options.opt, not VCC_NOOPT).  em_bound_constant
 * proves a small expression pure and constant within a budget before asking the constant evaluator,
 * so enumerators and casts of them fold but nothing with a call or a volatile read is traversed.
 * Called by the shared walker (walk/expression.c), arm64's and wasm's emitters.
 */
#include "../codegen-internal.h"

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
	if (!codegen_type_is_integer(c, x->ty) || codegen_type_is_bitint(c, x->ty) || codegen_type_size(c, x->ty) > 8)
		return false;
	switch (x->op) {
	case AST_NODE_NUMBER:
		return true;
	case AST_NODE_VARIABLE:
		return c->syms[x->a].kind == AST_SYMBOL_ENUMERATOR;
	case AST_NODE_CAST:
		return (x->aux == AST_CAST_INTEGER || x->aux == AST_CAST_NOP || x->aux == AST_CAST_BOOL) &&
		       em_bound_pure(c, x->a, budget);
	case AST_NODE_UNARY:
		return (x->aux == LEX_TOKEN_PLUS || x->aux == LEX_TOKEN_MINUS || x->aux == LEX_TOKEN_TILDE || x->aux == LEX_TOKEN_EXCLAMATION) &&
		       em_bound_pure(c, x->a, budget);
	case AST_NODE_BINARY:
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
	cc_value cv;

	if (x->op == AST_NODE_NUMBER) {
		*value = c->ipool[x->a];
		return true;
	}
	if (x->op == AST_NODE_VARIABLE && c->syms[x->a].kind == AST_SYMBOL_ENUMERATOR) {
		*value = c->syms[x->a].val;
		return true;
	}
	if ((x->op != AST_NODE_CAST && x->op != AST_NODE_UNARY && x->op != AST_NODE_BINARY) ||
	    !em_bound_pure(c, n, &budget) || !cc_eval(c, n, &cv) || cv.kind != CC_VALUE_INTEGER)
		return false;
	*value = cv.i;
	return true;
}

VPRIV int em_cmpbounds(codegen_context *e, uint32_t n, uint32_t *operand)
{
	cc *c = e->c;
	const node *x = N(n), *a, *b;
	int op;
	i128 v, min, max;
	uint64_t sz;

	if (!e->E->o->opt || e->E->noopt || x->op != AST_NODE_BINARY)
		return -1;
	op = x->aux;
	if (op != LEX_TOKEN_LESS && op != LEX_TOKEN_LESS_EQUAL && op != LEX_TOKEN_GREATER && op != LEX_TOKEN_GREATER_EQUAL)
		return -1;
	a = N(x->a); b = N(x->b);
	if (AST_QUALIFIED_TYPE_UNQUALIFIED(a->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(b->ty) || !codegen_type_is_integer(c, a->ty) || codegen_type_is_bitint(c, a->ty))
		return -1;
	sz = codegen_type_size(c, a->ty);
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
	v = codegen_integer_truncate(c, v, a->ty);
	if (codegen_type_is_signed(c, a->ty)) {
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
