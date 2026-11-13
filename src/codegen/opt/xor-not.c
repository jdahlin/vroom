/*
 * xor-not.c: x ^ -1 is ~x, after the usual integer conversions.
 *
 * em_xor_not(c, ty, op, rhs) is true when the operator is ^ and the constant, wrapped to the type
 * (em_trunc_to), is all ones; 4- and 8-byte integers.  Used by the shared walker, arm64's arithmetic
 * and wasm's emitter, which then emit the complement.
 */
#include "../emitimpl.h"

VPRIV bool em_xor_not(cc *c, qty ty, int op, i128 rhs)
{
	if (op != LEX_TOKEN_CARET || !is_integer(c, ty) || em_bitint(c, ty))
		return false;
	uint64_t size = tsize(c, ty);
	return (size == 4 || size == 8) && em_trunc_to(c, rhs, ty) == em_trunc_to(c, -1, ty);
}
