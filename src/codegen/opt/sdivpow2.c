/* Signed division needs a bias before its arithmetic shift: C rounds toward
   zero. For |d| = 2^k, bias = ((unsigned)(n >> (w-1))) >> (w-k).
   Quotient = (n+bias) >> k, negated for d<0; remainder =
   ((n+bias) & (|d|-1)) - bias. Callers retain operand evaluation. */
#include "../emitimpl.h"

VPRIV bool em_sdivpow2(cc *c, qty ty, int op, i128 rhs, em_sdivpow2_recipe *r)
{
	i128 d;
	uint64_t mag;

	if ((op != LEX_TOKEN_SLASH && op != LEX_TOKEN_PERCENT) || !is_integer(c, ty) ||
	    !is_signed(c, ty) || em_bitint(c, ty) || !tsize(c, ty) || tsize(c, ty) > 8)
		return false;
	d = em_trunc_to(c, rhs, ty);
	/* i128 contains every supported dividend/divisor, including INT64_MIN. */
	mag = (uint64_t)(d < 0 ? -d : d);
	if (!mag || (mag & (mag - 1)))
		return false;
	*r = (em_sdivpow2_recipe){ .bits = tsize(c, ty) <= 4 ? 32 : 64,
		.shift = __builtin_ctzll(mag), .negative = d < 0, .remainder = op == LEX_TOKEN_PERCENT };
	return true;
}
