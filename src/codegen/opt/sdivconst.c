/* Signed32/64 reciprocal recipes, using the positive-magnitude construction
 * from Hacker's Delight ch.10 / libdivide's signed branchfull generator.
 * C truncation toward zero needs the final sign-bit addition. Divisor sign
 * affects only quotient negation; remainder uses the positive quotient.
 * The equality case also needs no add: with H=2^(W-1), l=floor(log2 d),
 * m*d=2^(W-1+l)+2^l gives n*m/2^(W-1+l)=n/d+n/(d*H).
 * For 0<=n<H, writing n=a*d+r leaves 0<=r+n/H<d. For n=-u,
 * 0<u<=H, 0<r+u/H<=d; floor is -a-1 and the sign correction adds one.
 * Thus equality preserves every signed input, including the minimum. */
#include "../emitimpl.h"

VPRIV bool em_sdivconst32(int32_t divisor, bool remainder, em_sdivconst_recipe *r)
{
	uint32_t d = (uint32_t)(divisor < 0 ? -(int64_t)divisor : divisor);

	if (!d || !(d & (d - 1)))
		return false;
	*r = (em_sdivconst_recipe){ .divisor=d, .bits=32, .negative=divisor<0, .remainder=remainder };
	unsigned log = 31 - (unsigned)__builtin_clz(d);
	uint64_t numerator = UINT64_C(1) << (31 + log);
	uint64_t magic = numerator / d;
	uint32_t rem = (uint32_t)(numerator - magic * d);
	r->add = d - rem > (UINT32_C(1) << log);
	if (r->add)
		magic = magic * 2 + ((uint64_t)rem * 2 >= d);
	r->magic = (int32_t)(uint32_t)(magic + 1);
	r->shift = (uint8_t)(log - !r->add);
	return true;
}

/* Keep host u128 division out of the much cheaper signed32 construction. */
VPRIV bool em_sdivconst64(int64_t divisor, bool remainder, em_sdivconst_recipe *r)
{
	uint64_t d = (uint64_t)(divisor < 0 ? -(i128)divisor : divisor);

	if (!d || !(d & (d - 1)))
		return false;
	*r = (em_sdivconst_recipe){ .divisor=d, .bits=64, .negative=divisor<0, .remainder=remainder };
	unsigned log = 63 - (unsigned)__builtin_clzll(d);
	u128 numerator = (u128)1 << (63 + log);
	u128 magic = numerator / d;
	uint64_t rem = (uint64_t)(numerator - magic * d);
	r->add = d - rem > (UINT64_C(1) << log);
	if (r->add)
		magic = magic * 2 + ((u128)rem * 2 >= d);
	r->magic = (int64_t)(uint64_t)(magic + 1);
	r->shift = (uint8_t)(log - !r->add);
	return true;
}

VPRIV bool em_sdivconst(cc *c, qty ty, int op, i128 rhs, em_sdivconst_recipe *r)
{
	if ((op != LEX_TOKEN_SLASH && op != LEX_TOKEN_PERCENT) || !is_integer(c, ty) ||
	    !is_signed(c, ty) || em_bitint(c, ty))
		return false;
	unsigned size = (unsigned)tsize(c, ty);
	if (size == 4)
		return em_sdivconst32((int32_t)em_trunc_to(c, rhs, ty), op == LEX_TOKEN_PERCENT, r);
	if (size == 8)
		return em_sdivconst64((int64_t)em_trunc_to(c, rhs, ty), op == LEX_TOKEN_PERCENT, r);
	return false;
}
