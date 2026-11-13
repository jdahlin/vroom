/* Unsigned division/remainder by a power of two. Callers preserve operand
   evaluation; this helper only rewrites the operation and constant RHS. */
#include "../emitimpl.h"

VPRIV bool em_divpow2(cc *c, qty ty, int *op, i128 *rhs)
{
	uint64_t d;

	if ((*op != LEX_TOKEN_SLASH && *op != LEX_TOKEN_PERCENT) || !is_integer(c, ty) ||
	    is_signed(c, ty) || em_bitint(c, ty) || !tsize(c, ty) || tsize(c, ty) > 8)
		return false;
	d = (uint64_t)em_trunc_to(c, *rhs, ty);
	if (!d || (d & (d - 1)))
		return false;
	if (*op == LEX_TOKEN_SLASH) {
		*op = LEX_TOKEN_GREATER_GREATER;
		*rhs = __builtin_ctzll(d);
	} else {
		*op = LEX_TOKEN_AMPERSAND;
		*rhs = d - 1;
	}
	return true;
}
