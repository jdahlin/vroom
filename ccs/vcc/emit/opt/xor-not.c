/* Complement recognition after the usual integer conversions. */
#include "../emitimpl.h"

VPRIV bool em_xor_not(cc *c, qty ty, int op, i128 rhs)
{
	if (op != T_CARET || !is_integer(c, ty) || em_bitint(c, ty))
		return false;
	uint64_t size = tsize(c, ty);
	return (size == 4 || size == 8) && em_trunc_to(c, rhs, ty) == em_trunc_to(c, -1, ty);
}
