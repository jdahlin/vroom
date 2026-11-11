/* Cheap arithmetic rules shared by every target. Callers preserve operand
   evaluation; this helper only rewrites the operation and constant RHS. */
#include "../emitimpl.h"

VPRIV bool em_divpow2(cc *c, qty ty, int *op, i128 *rhs)
{
	uint64_t d;

	if ((*op != T_SLASH && *op != T_PERCENT) || !is_integer(c, ty) ||
	    is_signed(c, ty) || em_bitint(c, ty) || !tsize(c, ty) || tsize(c, ty) > 8)
		return false;
	d = (uint64_t)em_trunc_to(c, *rhs, ty);
	if (!d || (d & (d - 1)))
		return false;
	if (*op == T_SLASH) {
		*op = T_GREATERGREATER;
		*rhs = __builtin_ctzll(d);
	} else {
		*op = T_AMP;
		*rhs = d - 1;
	}
	return true;
}
