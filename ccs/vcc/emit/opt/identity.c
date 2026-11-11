/* Integer identity recipes shared by every target. */
#include "../emitimpl.h"

/* A recipe only: callers retain effects and normalize the retained value to ty. */
VPRIV int em_bin_identity(cc *c, qty ty, int op, i128 rhs)
{
	if (!is_integer(c, ty) || em_bitint(c, ty) || !tsize(c, ty) || tsize(c, ty) > 8)
		return EM_ID_NONE;
	/* Shift counts have their own promoted type, not the left operand's type. */
	if (op == T_LESSLESS || op == T_GREATERGREATER)
		return rhs == 0 ? EM_ID_LEFT : EM_ID_NONE;
	rhs = em_trunc_to(c, rhs, ty);
	switch (op) {
	case T_PLUS: case T_MINUS: case T_CARET:
		return rhs == 0 ? EM_ID_LEFT : EM_ID_NONE;
	case T_STAR:
		return rhs == 0 ? EM_ID_ZERO : rhs == 1 ? EM_ID_LEFT : EM_ID_NONE;
	case T_SLASH:
		return rhs == 1 ? EM_ID_LEFT : EM_ID_NONE;
	case T_PERCENT:
		return rhs == 1 ? EM_ID_ZERO : EM_ID_NONE;
	case T_AMP:
		return rhs == 0 ? EM_ID_ZERO : rhs == em_trunc_to(c, -1, ty) ? EM_ID_LEFT : EM_ID_NONE;
	case T_PIPE:
		return rhs == 0 ? EM_ID_LEFT : rhs == em_trunc_to(c, -1, ty) ? EM_ID_ONES : EM_ID_NONE;
	default:
		return EM_ID_NONE;
	}
}
