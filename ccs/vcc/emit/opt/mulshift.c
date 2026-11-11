/* A power-of-two low product is a machine shift, including signed products
 * whenever the original multiplication is defined. No C shift AST is made. */
#include "../emitimpl.h"

VPRIV bool em_mulshift(cc *c, qty ty, int *op, i128 *rhs)
{
	if (*op != T_STAR || !is_integer(c, ty) || em_bitint(c, ty))
		return false;
	uint64_t size = tsize(c, ty);
	if (size != 4 && size != 8)
		return false;
	uint64_t factor = size == 4 ? (uint32_t)*rhs : (uint64_t)*rhs;
	if (factor < 2 || (factor & (factor - 1)))
		return false;
	*op = T_LESSLESS;
	*rhs = __builtin_ctzll(factor);
	return true;
}
