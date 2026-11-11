/* Unsigned32 rounded reciprocal recipes. The construction follows the
 * round-up/add-correction algorithm described in Hacker's Delight ch.10 and
 * libdivide's unsigned32 generator (https://github.com/ridiculousfish/libdivide).
 * No division by a runtime value or machine-register assumptions live here. */
#include "../emitimpl.h"

VPRIV bool em_udivconst32(uint32_t d, bool remainder, em_udivconst_recipe *r)
{
	if (!d || !(d & (d - 1)))
		return false; /* Zero and powers of two keep their existing paths. */
	*r = (em_udivconst_recipe){ .divisor=d, .remainder=remainder };
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

VPRIV bool em_udivconst(cc *c, qty ty, int op, i128 rhs, em_udivconst_recipe *r)
{
	if ((op != T_SLASH && op != T_PERCENT) || !is_integer(c, ty) ||
	    is_signed(c, ty) || em_bitint(c, ty) || tsize(c, ty) != 4)
		return false;
	return em_udivconst32((uint32_t)em_trunc_to(c, rhs, ty), op == T_PERCENT, r);
}
