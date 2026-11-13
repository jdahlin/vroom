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
	while (N(n)->op == N_CAST)
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
	if (((QQ(x->ty) & Q_VOLATILE) || (T(c, x->ty)->flags & TF_ATOMIC)) &&
	    (x->op == N_VAR || x->op == N_DEREF || x->op == N_MEMBER || x->op == N_INDEX))
		return true;
	switch (x->op) {
	case N_NUM: case N_FNUM: case N_STR: case N_VAR: case N_ZERO: case N_LABELADDR:
		return false;
	case N_CALL: case N_ASSIGN: case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
	case N_ASM:
		return true;
	case N_BUILTIN:
		switch (x->aux) {
		case BI_VA_START: case BI_VA_COPY: case BI_VA_END: case BI_ALLOCA: case BI_TRAP: case BI_DEBUGTRAP:
		case BI_UNREACHABLE:
		case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW: case BI_MUL_OVERFLOW:	/* store the result */
		case BI_ADDC: case BI_SUBC:
			return true;
		default:
			if (x->aux >= BI_ATOMIC_LOAD_N && x->aux <= BI_ATOMIC_SIGNAL_FENCE)
				return true;
			if (x->aux == BI_CONSTANT_P)
				return false;	/* its operand is not evaluated */
			if (x->aux >= BI_ARM_NOP && x->aux <= BI_ARM_TRAP)
				return true;
			for (uint32_t i = 0; i < x->c; i++)
				if (side_effects(c, c->lists[x->b + i]))
					return true;
			return false;
		}
	case N_MEMBER: case N_DEREF: case N_UNARY: case N_CAST: case N_ADDR: case N_COMPLIT:
	case N_STMTEXPR: case N_EXPR: case N_RETURN:
		return side_effects(c, x->a);
	case N_INDEX: case N_BINARY: case N_LOGAND: case N_LOGOR: case N_COMMA: case N_CPLX:
		return side_effects(c, x->a) || side_effects(c, x->b);
	case N_COND: case N_IF:
		return side_effects(c, x->a) || side_effects(c, x->b) || side_effects(c, x->c);
	case N_DECL:
		return side_effects(c, x->b);
	case N_BLOCK: case N_INIT:
		if (x->op == N_INIT && x->aux == 1) {
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
