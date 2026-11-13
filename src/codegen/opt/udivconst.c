/* Unsigned32/64 rounded reciprocal recipes. The construction follows the
 * round-up/add-correction algorithm described in Hacker's Delight ch.10 and
 * libdivide's unsigned32 generator (https://github.com/ridiculousfish/libdivide).
 * No division by a runtime value or machine-register assumptions live here. */
#include "../emitimpl.h"

VPRIV bool em_udivconst32(uint32_t d, bool remainder, em_udivconst_recipe *r)
{
	if (!d || !(d & (d - 1)))
		return false; /* Zero and powers of two keep their existing paths. */
	*r = (em_udivconst_recipe){ .divisor=d, .bits=32, .remainder=remainder };
	if (d > UINT32_C(0x80000000)) {
		r->compare = true; /* The quotient is at most one. */
		return true;
	}
	unsigned shift = 31 - (unsigned)__builtin_clz(d);
	uint64_t numerator = UINT64_C(1) << (32 + shift);
	uint64_t multiplier = numerator / d;
	uint32_t residue = (uint32_t)(numerator - multiplier * d);
	/* A rounded-up reciprocal needs no correction when its error is small
	 * enough over the full unsigned32 input domain. Otherwise double the
	 * reciprocal precision, keeping the implicit 33rd bit in the add step. */
	if (d - residue >= (UINT32_C(1) << shift)) {
		multiplier = multiplier * 2 + ((uint64_t)residue * 2 >= d);
		r->add = true;
	}
	r->magic = (uint32_t)(multiplier + 1);
	r->shift = (uint8_t)shift;
	return true;
}

/* This wider construction is isolated: unsigned32 never pays for host u128
 * division. With d<2^63, numerator<=2^126 and the doubled multiplier fits. */
VPRIV bool em_udivconst64(uint64_t d, bool remainder, em_udivconst_recipe *r)
{
	if (!d || !(d & (d - 1)))
		return false;
	*r = (em_udivconst_recipe){ .divisor=d, .bits=64, .remainder=remainder };
	if (d > (UINT64_C(1) << 63)) {
		r->compare = true;
		return true;
	}
	unsigned shift = 63 - (unsigned)__builtin_clzll(d);
	u128 numerator = (u128)1 << (64 + shift);
	u128 multiplier = numerator / d;
	uint64_t residue = (uint64_t)(numerator - multiplier * d);
	if (d - residue >= (UINT64_C(1) << shift)) {
		multiplier = multiplier * 2 + ((u128)residue * 2 >= d);
		r->add = true;
	}
	r->magic = (uint64_t)(multiplier + 1);
	r->shift = (uint8_t)shift;
	return true;
}

VPRIV bool em_udivconst(cc *c, qty ty, int op, i128 rhs, em_udivconst_recipe *r)
{
	if ((op != LEX_TOKEN_SLASH && op != LEX_TOKEN_PERCENT) || !is_integer(c, ty) ||
	    is_signed(c, ty) || em_bitint(c, ty))
		return false;
	unsigned size = (unsigned)tsize(c, ty);
	if (size == 4)
		return em_udivconst32((uint32_t)em_trunc_to(c, rhs, ty), op == LEX_TOKEN_PERCENT, r);
	if (size == 8)
		return em_udivconst64((uint64_t)em_trunc_to(c, rhs, ty), op == LEX_TOKEN_PERCENT, r);
	return false;
}
