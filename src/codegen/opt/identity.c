/* Integer identity recipes shared by every target. */
#include "../emitimpl.h"

/* A recipe only: callers retain effects and normalize the retained value to ty. */
VPRIV int em_bin_identity(cc *c, qty ty, int op, i128 rhs)
{
	if (!is_integer(c, ty) || em_bitint(c, ty) || !tsize(c, ty) || tsize(c, ty) > 8)
		return EM_ID_NONE;
	/* Shift counts have their own promoted type, not the left operand's type. */
	if (op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER)
		return rhs == 0 ? EM_ID_LEFT : EM_ID_NONE;
	rhs = em_trunc_to(c, rhs, ty);
	switch (op) {
	case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS: case LEX_TOKEN_CARET:
		return rhs == 0 ? EM_ID_LEFT : EM_ID_NONE;
	case LEX_TOKEN_STAR:
		return rhs == 0 ? EM_ID_ZERO : rhs == 1 ? EM_ID_LEFT : EM_ID_NONE;
	case LEX_TOKEN_SLASH:
		return rhs == 1 ? EM_ID_LEFT : EM_ID_NONE;
	case LEX_TOKEN_PERCENT:
		return rhs == 1 ? EM_ID_ZERO : EM_ID_NONE;
	case LEX_TOKEN_AMPERSAND:
		return rhs == 0 ? EM_ID_ZERO : rhs == em_trunc_to(c, -1, ty) ? EM_ID_LEFT : EM_ID_NONE;
	case LEX_TOKEN_PIPE:
		return rhs == 0 ? EM_ID_LEFT : rhs == em_trunc_to(c, -1, ty) ? EM_ID_ONES : EM_ID_NONE;
	default:
		return EM_ID_NONE;
	}
}
