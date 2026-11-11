/* Single shifted-add multiplication recipes. Targets retain multiplication
 * unless they can select a profitable machine operation (wasm keeps mul). */
#include "../emitimpl.h"

VPRIV unsigned em_mulconst(unsigned bits, i128 rhs)
{
	if (bits != 32 && bits != 64)
		return 0;
	switch (bits == 32 ? (uint32_t)rhs : (uint64_t)rhs) {
	case 3: return 1;
	case 5: return 2;
	case 9: return 3;
	default: return 0;
	}
}

/* (2^a + 1) << post, post > 0: x + (x << a), then a shift by post (clang's 24 = 3 << 3).
 * Returns a, or zero (with *post 0) to retain the multiply. */
VPRIV unsigned em_mulconst_shl(unsigned bits, i128 rhs, unsigned *post)
{
	uint64_t m = bits == 32 ? (uint32_t)rhs : (uint64_t)rhs;
	unsigned shift;

	*post = 0;
	if ((bits != 32 && bits != 64) || !m || (m & 1))
		return 0;
	*post = (unsigned)__builtin_ctzll(m);
	shift = em_mulconst(bits, (i128)(m >> *post));
	if (!shift || (m >> *post) == m)
		*post = 0, shift = 0;
	return shift;
}
