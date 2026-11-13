/*
 * __float128 (IEEE binary128) arithmetic, libgcc's "Soft float library
 * routines": __addtf3 (a + b), __subtf3 (a - b), __multf3 (a * b),
 * __divtf3 (a / b), each correctly rounded to nearest, ties to even, with
 * gradual underflow.
 *
 * Special operands follow IEEE 754: a NaN operand gives that NaN, quieted
 * (the first operand's when both are NaNs); an invalid operation (inf - inf,
 * 0 * inf, 0 / 0, inf / inf) gives the default NaN, which here is positive
 * with only the quiet bit set (the choice the usual runtime makes; the x87
 * and SSE hardware's default NaN is negative).  a - b is a + (-b), so a NaN b
 * comes back with its sign flipped.  An exact zero sum is +0 unless both
 * operands are -0.
 *
 * The operands are taken apart by __vccrt_quad_unpack (normalised, so
 * subnormals need no special case) and the exact or sticky result is
 * rounded once by __vccrt_round.
 */
#include "runtime-internal.h"

static __float128 quieted(__float128 a)
{
	vccrt_quad_bits b = { .value = a };
	b.words.half.high |= 1ull << 47;
	return b.value;
}

static __float128 default_nan(void)
{
	vccrt_quad_bits b;
	b.words.half.low = 0;
	b.words.half.high = 0x7fff800000000000ull;
	return b.value;
}

__float128 __addtf3(__float128 a, __float128 b)
{
	vccrt_unpacked x, y, sum;
	int kind_a = __vccrt_quad_unpack(a, &x), kind_b = __vccrt_quad_unpack(b, &y);
	vccrt_u128 larger, smaller;
	int distance;

	if (kind_a == 3)
		return quieted(a);
	if (kind_b == 3)
		return quieted(b);
	if (kind_a == 2)
		return kind_b == 2 && x.negative != y.negative ? default_nan() : a;
	if (kind_b == 2)
		return b;
	if (kind_a == 1)
		return kind_b == 1 ? __vccrt_quad_special(x.negative && y.negative, 0) : b;
	if (kind_b == 1)
		return a;

	if (y.exponent > x.exponent || (y.exponent == x.exponent && y.significand > x.significand)) {
		vccrt_unpacked t = x;
		x = y;
		y = t;
	}
	/* one bit of headroom for a carry; the low 15 bits are zero, so nothing is lost */
	larger = x.significand >> 1;
	smaller = y.significand >> 1;
	distance = x.exponent - y.exponent;
	if (distance >= 128) {
		smaller = 1;	/* only sticky: below every bit that can round */
	} else if (distance) {
		int sticky = (smaller << (128 - distance)) != 0;
		smaller = smaller >> distance | (vccrt_u128)sticky;	/* jammed into bit 0 */
	}
	sum.negative = x.negative;
	sum.sticky = 0;
	if (x.negative == y.negative) {
		sum.significand = larger + smaller;
	} else {
		sum.significand = larger - smaller;
		if (!sum.significand)
			return __vccrt_quad_special(0, 0);
	}
	{
		int zeros = vccrt_clz128(sum.significand);
		sum.significand <<= zeros;
		sum.exponent = x.exponent + 1 - zeros;
	}
	return __vccrt_quad_pack(sum.negative, __vccrt_round(sum, VCCRT_QUAD));
}

__float128 __subtf3(__float128 a, __float128 b)
{
	vccrt_quad_bits negated = { .value = b };
	negated.words.half.high ^= 1ull << 63;
	return __addtf3(a, negated.value);
}

__float128 __multf3(__float128 a, __float128 b)
{
	vccrt_unpacked x, y, product;
	int kind_a = __vccrt_quad_unpack(a, &x), kind_b = __vccrt_quad_unpack(b, &y);
	int negative = x.negative != y.negative;
	vccrt_words p, q;
	vccrt_u128 low_low, low_high, high_low, high_high, middle, high, low;

	if (kind_a == 3)
		return quieted(a);
	if (kind_b == 3)
		return quieted(b);
	if (kind_a == 2 || kind_b == 2)
		return kind_a == 1 || kind_b == 1 ? default_nan() : __vccrt_quad_special(negative, 1);
	if (kind_a == 1 || kind_b == 1)
		return __vccrt_quad_special(negative, 0);

	/* the 256-bit product of the two significands from four 64 x 64 products */
	p.all = x.significand;
	q.all = y.significand;
	low_low = (vccrt_u128)p.half.low * q.half.low;
	low_high = (vccrt_u128)p.half.low * q.half.high;
	high_low = (vccrt_u128)p.half.high * q.half.low;
	high_high = (vccrt_u128)p.half.high * q.half.high;
	middle = (low_low >> 64) + (vccrt_u64)low_high + (vccrt_u64)high_low;	/* < 3 * 2^64: no overflow */
	low = (middle << 64) | (vccrt_u64)low_low;
	high = high_high + (low_high >> 64) + (high_low >> 64) + (middle >> 64);

	product.negative = negative;
	product.exponent = x.exponent + y.exponent + 1;
	if (!(high >> 127)) {	/* the product of two values in [1, 2) is in [1, 4) */
		high = high << 1 | low >> 127;
		low <<= 1;
		product.exponent--;
	}
	product.significand = high;
	product.sticky = low != 0;
	return __vccrt_quad_pack(negative, __vccrt_round(product, VCCRT_QUAD));
}

__float128 __divtf3(__float128 a, __float128 b)
{
	vccrt_unpacked x, y, quotient;
	int kind_a = __vccrt_quad_unpack(a, &x), kind_b = __vccrt_quad_unpack(b, &y);
	int negative = x.negative != y.negative, i;
	vccrt_u128 dividend, divisor, bits = 0;

	if (kind_a == 3)
		return quieted(a);
	if (kind_b == 3)
		return quieted(b);
	if (kind_a == 2)
		return kind_b == 2 ? default_nan() : __vccrt_quad_special(negative, 1);
	if (kind_b == 2)
		return __vccrt_quad_special(negative, 0);
	if (kind_b == 1)
		return kind_a == 1 ? default_nan() : __vccrt_quad_special(negative, 1);
	if (kind_a == 1)
		return __vccrt_quad_special(negative, 0);

	/* long division of the 113-bit significands: 116 quotient bits and a sticky remainder */
	dividend = x.significand >> 15;
	divisor = y.significand >> 15;
	quotient.exponent = x.exponent - y.exponent;
	if (dividend < divisor) {
		dividend <<= 1;
		quotient.exponent--;
	}
	for (i = 0; i < 116; i++) {
		bits <<= 1;
		if (dividend >= divisor) {
			dividend -= divisor;
			bits |= 1;
		}
		dividend <<= 1;
	}
	quotient.negative = negative;
	quotient.significand = bits << 12;
	quotient.sticky = dividend != 0;
	return __vccrt_quad_pack(negative, __vccrt_round(quotient, VCCRT_QUAD));
}
