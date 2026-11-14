/*
 * __float128 comparisons, libgcc's "Soft float library routines":
 *
 *   __eqtf2  zero if neither is a NaN and a == b, else nonzero
 *   __netf2  zero if neither is a NaN and a == b, else nonzero
 *   __lttf2  negative if neither is a NaN and a < b, else zero or positive
 *   __letf2  zero or negative if neither is a NaN and a <= b, else positive
 *   __gttf2  positive if neither is a NaN and a > b, else zero or negative
 *   __getf2  zero or positive if neither is a NaN and a >= b, else negative
 *   __unordtf2  nonzero if either is a NaN
 *
 * The ordered ones all return -1, 0 or 1 for less, equal, greater (+0 and
 * -0 are equal); unordered gives 1 for eq, ne, lt and le, -1 for gt and ge,
 * so that the caller's test against zero is false for every ordered
 * relation.  These are the exact values the usual runtime returns.
 */
#include "runtime-internal.h"

static int compare(__float128 a, __float128 b, int unordered)
{
	vccrt_quad_bits x = { .value = a }, y = { .value = b };
	int negative_a = (int)(x.words.half.high >> 63), negative_b = (int)(y.words.half.high >> 63);
	vccrt_u128 sign = (vccrt_u128)1 << 127, infinity = (vccrt_u128)0x7fff << 112;
	vccrt_u128 abs_a = x.words.all & ~sign, abs_b = y.words.all & ~sign;
	int less;

	if (abs_a > infinity || abs_b > infinity)
		return unordered;
	if (!(abs_a | abs_b))
		return 0;
	if (negative_a != negative_b)
		return negative_a ? -1 : 1;
	if (abs_a == abs_b)
		return 0;
	less = abs_a < abs_b;
	if (negative_a)
		less = !less;
	return less ? -1 : 1;
}

int __eqtf2(__float128 a, __float128 b)
{
	return compare(a, b, 1);
}

int __netf2(__float128 a, __float128 b)
{
	return compare(a, b, 1);
}

int __lttf2(__float128 a, __float128 b)
{
	return compare(a, b, 1);
}

int __letf2(__float128 a, __float128 b)
{
	return compare(a, b, 1);
}

int __gttf2(__float128 a, __float128 b)
{
	return compare(a, b, -1);
}

int __getf2(__float128 a, __float128 b)
{
	return compare(a, b, -1);
}

int __unordtf2(__float128 a, __float128 b)
{
	vccrt_quad_bits x = { .value = a }, y = { .value = b };
	vccrt_u128 sign = (vccrt_u128)1 << 127, infinity = (vccrt_u128)0x7fff << 112;
	return (x.words.all & ~sign) > infinity || (y.words.all & ~sign) > infinity;
}
