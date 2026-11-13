/*
 * expression-effects.c: strip_casts and side_effects: what evaluating an expression does.
 *
 * side_effects follows clang's Expr::HasSideEffects, with "possible" effects counted: calls, writes,
 * increments, inline asm, volatile and atomic accesses, and the builtins that store or trap.  The
 * answer errs towards true (loops, gotos, unknown nodes: yes), so callers use it only to prove that
 * dropping or reordering an expression is safe, never to find dead code from a `false`.  Used by the
 * shared walker, the prepass and the arm64, x86-64 and wasm emitters.
 */
#include "emitimpl.h"

VPRIV uint32_t strip_casts(cc *c, uint32_t n)
{
	while (N(n)->op == AST_NODE_CAST)
		n = N(n)->a;
	return n;
}

/* clang's Expr::HasSideEffects (with possible effects): calls, writes, volatile reads */
VPRIV bool side_effects(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return false;
	x = N(n);
	if (((AST_QUALIFIED_TYPE_QUALIFIERS(x->ty) & AST_QUALIFIER_VOLATILE) || (T(c, x->ty)->flags & AST_TYPE_FLAG_ATOMIC)) &&
	    (x->op == AST_NODE_VARIABLE || x->op == AST_NODE_DEREFERENCE || x->op == AST_NODE_MEMBER || x->op == AST_NODE_INDEX))
		return true;
	switch (x->op) {
	case AST_NODE_NUMBER: case AST_NODE_FLOAT_NUMBER: case AST_NODE_STRING: case AST_NODE_VARIABLE: case AST_NODE_ZERO: case AST_NODE_LABEL_ADDRESS:
		return false;
	case AST_NODE_CALL: case AST_NODE_ASSIGN: case AST_NODE_PRE_INCREMENT: case AST_NODE_PRE_DECREMENT: case AST_NODE_POST_INCREMENT: case AST_NODE_POST_DECREMENT:
	case AST_NODE_ASM:
		return true;
	case AST_NODE_BUILTIN:
		switch (x->aux) {
		case CC_BUILTIN_VA_START: case CC_BUILTIN_VA_COPY: case CC_BUILTIN_VA_END: case CC_BUILTIN_ALLOCA: case CC_BUILTIN_TRAP: case CC_BUILTIN_DEBUGTRAP:
		case CC_BUILTIN_UNREACHABLE:
		case CC_BUILTIN_ADD_OVERFLOW: case CC_BUILTIN_SUB_OVERFLOW: case CC_BUILTIN_MUL_OVERFLOW:	/* store the result */
		case CC_BUILTIN_ADDC: case CC_BUILTIN_SUBC:
			return true;
		default:
			if (x->aux >= CC_BUILTIN_ATOMIC_LOAD_N && x->aux <= CC_BUILTIN_ATOMIC_SIGNAL_FENCE)
				return true;
			if (x->aux == CC_BUILTIN_CONSTANT_P)
				return false;	/* its operand is not evaluated */
			if (x->aux >= CC_BUILTIN_ARM_NOP && x->aux <= CC_BUILTIN_ARM_TRAP)
				return true;
			for (uint32_t i = 0; i < x->c; i++)
				if (side_effects(c, c->lists[x->b + i]))
					return true;
			return false;
		}
	case AST_NODE_MEMBER: case AST_NODE_DEREFERENCE: case AST_NODE_UNARY: case AST_NODE_CAST: case AST_NODE_ADDRESS: case AST_NODE_COMPOUND_LITERAL:
	case AST_NODE_STATEMENT_EXPRESSION: case AST_NODE_EXPRESSION: case AST_NODE_RETURN:
		return side_effects(c, x->a);
	case AST_NODE_INDEX: case AST_NODE_BINARY: case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR: case AST_NODE_COMMA: case AST_NODE_COMPLEX:
		return side_effects(c, x->a) || side_effects(c, x->b);
	case AST_NODE_CONDITIONAL: case AST_NODE_IF:
		return side_effects(c, x->a) || side_effects(c, x->b) || side_effects(c, x->c);
	case AST_NODE_DECLARATION:
		return side_effects(c, x->b);
	case AST_NODE_BLOCK: case AST_NODE_INITIALIZER:
		if (x->op == AST_NODE_INITIALIZER && x->aux == 1) {
			for (uint32_t i = 0; i < x->c; i++)
				if (side_effects(c, c->lists[x->b + 3 * i + 2]))
					return true;
			return false;
		}
		for (uint32_t i = 0; i < x->c; i++)
			if (side_effects(c, c->lists[x->b + i]))
				return true;
		return false;
	default:
		return true;	/* loops, gotos, labels, va_arg: conservatively yes */
	}
}
