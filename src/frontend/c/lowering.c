/* vcc front end: helpers that lowering of complex, wide atomics and _BitInt casts build AST with */
#include "cc-internal.h"

CC_PRIVATE uint32_t cc_lowering_zero(cc *c, ast_qualified_type t, uint32_t loc)
{
	return cc_type_is_float(c, t) ? cc_node_float_new(c, 0, AST_QUALIFIED_TYPE_UNQUALIFIED(t), loc) : cc_node_integer_new(c, 0, AST_QUALIFIED_TYPE_UNQUALIFIED(t), loc);
}

/* evaluating n twice is the same as once (and cheap) */
CC_PRIVATE bool cc_lowering_is_pure(cc *c, uint32_t n, int d)
{
	const node *x = AST_NODE(n);

	if (d > 8)
		return false;
	switch (x->op) {
	case AST_NODE_NUMBER: case AST_NODE_FLOAT_NUMBER:
		return true;
	case AST_NODE_BUILTIN: {
		cc_value v;

		return cc_eval(c, n, &v) && (v.kind == CC_VALUE_INTEGER || v.kind == CC_VALUE_FLOAT);
	}
	case AST_NODE_VARIABLE:
		return !(AST_QUALIFIED_TYPE_QUALIFIERS(x->ty) & AST_QUALIFIER_VOLATILE);
	case AST_NODE_MEMBER: case AST_NODE_CAST: case AST_NODE_UNARY: case AST_NODE_DEREFERENCE:
		return !(AST_QUALIFIED_TYPE_QUALIFIERS(x->ty) & AST_QUALIFIER_VOLATILE) && cc_lowering_is_pure(c, x->a, d + 1);
	case AST_NODE_BINARY: case AST_NODE_INDEX: case AST_NODE_COMPLEX: case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR: case AST_NODE_COMMA:
		return cc_lowering_is_pure(c, x->a, d + 1) && cc_lowering_is_pure(c, x->b, d + 1);
	default:
		return false;
	}
}

/* n, or a temporary holding it (*pre: the assignment to run first) */
CC_PRIVATE uint32_t cc_lowering_once(cc *c, uint32_t n, uint32_t *pre)
{
	ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(n)->ty);
	uint32_t s, v;

	*pre = 0;
	/* A constant conditional must not acquire an automatic temporary merely
	   because its selected complex value will be used twice. */
	if (AST_NODE(n)->op == AST_NODE_CONDITIONAL && cc_lowering_is_pure(c, AST_NODE(n)->a, 0)) {
		cc_value cv;
		if (cc_eval(c, AST_NODE(n)->a, &cv) && (cv.kind == CC_VALUE_INTEGER || cv.kind == CC_VALUE_FLOAT)) {
			bool yes = cv.kind == CC_VALUE_INTEGER ? cv.i != 0 : cv.ld ? vsoftfloat_is_nonzero(cv.q) : cv.f != 0;
			return cc_lowering_once(c, yes ? (AST_NODE(n)->b ? AST_NODE(n)->b : AST_NODE(n)->a) : AST_NODE(n)->c, pre);
		}
	}
	if (cc_lowering_is_pure(c, n, 0))
		return n;
	s = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, t, AST_NODE(n)->loc);
	c->syms[s].sc = AST_STORAGE_AUTO;
	v = cc_node_new(c, AST_NODE_VARIABLE, t, AST_NODE(n)->loc);
	AST_NODE(v)->a = s;
	AST_NODE(v)->flags = AST_NODE_FLAG_LVALUE;
	*pre = cc_node_new_operands(c, AST_NODE_ASSIGN, t, AST_NODE(n)->loc, v, n);
	return v;
}

CC_PRIVATE uint32_t cc_lowering_sequence(cc *c, uint32_t pre, uint32_t n)
{
	return pre ? cc_node_new_operands(c, AST_NODE_COMMA, AST_NODE(n)->ty, AST_NODE(n)->loc, pre, n) : n;
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
	uint32_t r = cc_node_new(c, AST_NODE_UNARY, AST_NODE(n)->ty, AST_NODE(n)->loc);

	AST_NODE(r)->aux = LEX_TOKEN_MINUS;
	AST_NODE(r)->a = n;
	return r;
}

/* "invalid operands to binary expression ('L' and 'R')", ranges on both */
CC_PRIVATE void cc_operands_invalid_report(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	diag_span s[2] = { cc_node_range(c, l), cc_node_range(c, r) };

	if ((AST_NODE(l)->flags | AST_NODE(r)->flags) & AST_NODE_FLAG_ERROR)
		return;
	c->dms = cc_punctuator_spelling(op);
	report(c, "error", loc, s, 2, NULL, 0, "invalid operands to binary expression ('%s' and '%s')",
	       tyname(c, AST_NODE(l)->ty), tyname(c, AST_NODE(r)->ty));
}
