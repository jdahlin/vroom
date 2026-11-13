/*
 * mulshift.c: a multiplication by a power of two is a left shift.
 *
 * em_mulshift(c, ty, &op, &rhs) rewrites the operator and the constant in place for 4- and 8-byte
 * integers, signed included: the low bits of the product are the same whenever the multiplication is
 * defined, and no shift node is built, so the caller's evaluation order and diagnostics are
 * unchanged.  Used by the shared walker and wasm's emitter.
 */
#include "../emitimpl.h"

VPRIV bool em_mulshift(cc *c, qty ty, int *op, i128 *rhs)
{
	if (*op != LEX_TOKEN_STAR || !is_integer(c, ty) || em_bitint(c, ty))
		return false;
	uint64_t size = tsize(c, ty);
	if (size != 4 && size != 8)
		return false;
	uint64_t factor = size == 4 ? (uint32_t)*rhs : (uint64_t)*rhs;
	if (factor < 2 || (factor & (factor - 1)))
		return false;
	*op = LEX_TOKEN_LESS_LESS;
	*rhs = __builtin_ctzll(factor);
	return true;
}
