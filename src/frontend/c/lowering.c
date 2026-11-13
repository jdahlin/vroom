/* vcc front end: helpers that lowering of complex, wide atomics and _BitInt casts build AST with */
#include "cc-internal.h"

CC_PRIVATE uint32_t cc_lowering_zero(cc *c, qty t, uint32_t loc)
{
	return cc_type_is_float(c, t) ? cc_node_float_new(c, 0, QUNQ(t), loc) : cc_node_integer_new(c, 0, QUNQ(t), loc);
}

/* evaluating n twice is the same as once (and cheap) */
CC_PRIVATE bool cc_lowering_is_pure(cc *c, uint32_t n, int d)
{
	const node *x = AST_NODE(n);

	if (d > 8)
		return false;
	switch (x->op) {
	case N_NUM: case N_FNUM:
		return true;
	case N_BUILTIN: {
		cval v;

		return cc_eval(c, n, &v) && (v.kind == CV_INT || v.kind == CV_FLT);
	}
	case N_VAR:
		return !(QQ(x->ty) & Q_VOLATILE);
	case N_MEMBER: case N_CAST: case N_UNARY: case N_DEREF:
		return !(QQ(x->ty) & Q_VOLATILE) && cc_lowering_is_pure(c, x->a, d + 1);
	case N_BINARY: case N_INDEX: case N_CPLX: case N_LOGAND: case N_LOGOR: case N_COMMA:
		return cc_lowering_is_pure(c, x->a, d + 1) && cc_lowering_is_pure(c, x->b, d + 1);
	default:
		return false;
	}
}

/* n, or a temporary holding it (*pre: the assignment to run first) */
CC_PRIVATE uint32_t cc_lowering_once(cc *c, uint32_t n, uint32_t *pre)
{
	qty t = QUNQ(AST_NODE(n)->ty);
	uint32_t s, v;

	*pre = 0;
	/* A constant conditional must not acquire an automatic temporary merely
	   because its selected complex value will be used twice. */
	if (AST_NODE(n)->op == N_COND && cc_lowering_is_pure(c, AST_NODE(n)->a, 0)) {
		cval cv;
		if (cc_eval(c, AST_NODE(n)->a, &cv) && (cv.kind == CV_INT || cv.kind == CV_FLT)) {
			bool yes = cv.kind == CV_INT ? cv.i != 0 : cv.ld ? vsoftfloat_is_nonzero(cv.q) : cv.f != 0;
			return cc_lowering_once(c, yes ? (AST_NODE(n)->b ? AST_NODE(n)->b : AST_NODE(n)->a) : AST_NODE(n)->c, pre);
		}
	}
	if (cc_lowering_is_pure(c, n, 0))
		return n;
	s = cc_symbol_new(c, S_VAR, 0, t, AST_NODE(n)->loc);
	c->syms[s].sc = SC_AUTO;
	v = cc_node_new(c, N_VAR, t, AST_NODE(n)->loc);
	AST_NODE(v)->a = s;
	AST_NODE(v)->flags = NF_LVALUE;
	*pre = cc_node_new_operands(c, N_ASSIGN, t, AST_NODE(n)->loc, v, n);
	return v;
}

CC_PRIVATE uint32_t cc_lowering_sequence(cc *c, uint32_t pre, uint32_t n)
{
	return pre ? cc_node_new_operands(c, N_COMMA, AST_NODE(n)->ty, AST_NODE(n)->loc, pre, n) : n;
}

/* a condition: complex values test both parts */
CC_PRIVATE uint32_t cc_condition_scalar(cc *c, uint32_t n, const char *what)
{
	if (cc_type_is_complex(c, AST_NODE(n)->ty))
		return cc_complex_to_bool(c, n);
	cc_node_require_scalar(c, n, what);
	return n;
}

CC_PRIVATE uint32_t neg(cc *c, uint32_t n)
{
	uint32_t r = cc_node_new(c, N_UNARY, AST_NODE(n)->ty, AST_NODE(n)->loc);

	AST_NODE(r)->aux = LEX_TOKEN_MINUS;
	AST_NODE(r)->a = n;
	return r;
}

/* "invalid operands to binary expression ('L' and 'R')", ranges on both */
CC_PRIVATE void cc_operands_invalid_report(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	diag_span s[2] = { cc_node_range(c, l), cc_node_range(c, r) };

	if ((AST_NODE(l)->flags | AST_NODE(r)->flags) & NF_ERR)
		return;
	c->dms = cc_punctuator_spelling(op);
	report(c, "error", loc, s, 2, NULL, 0, "invalid operands to binary expression ('%s' and '%s')",
	       tyname(c, AST_NODE(l)->ty), tyname(c, AST_NODE(r)->ty));
}
