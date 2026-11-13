/*
 * divpow2.c: unsigned division and remainder by a power of two become a shift and a mask.
 *
 * em_divpow2(c, ty, &op, &rhs) rewrites the operator and the constant in place (x / 8 to x >> 3,
 * x % 8 to x & 7) and returns true; it does not touch operands, so the caller keeps its evaluation
 * order.  Unsigned types of up to 8 bytes only (signed division needs a bias: sdivpow2.c).  Called by
 * the shared walker, arm64's arithmetic and wasm's emitter.
 */
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
