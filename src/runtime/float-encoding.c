/*
 * The bit layouts of the floating formats: taking a value apart into its
 * class and an exact vccrt_unpacked, putting a rounded value or a NaN back
 * together.  The IEEE interchange formats (binary16, bfloat16, binary32,
 * binary64, binary128) share one implementation with an implicit leading
 * bit; the x87 80-bit format, whose significand has an explicit integer bit,
 * has its own.
 *
 * NaNs: a NaN's payload (the fraction bits below the quiet bit) is carried
 * left-aligned, so a conversion keeps its top bits (IEEE 754-2019 6.2.3);
 * whether the quiet bit is forced is the caller's choice (vccrt_nan.quiet).
 * x87 encodings that IEEE has no counterpart for (unnormals, pseudo-NaNs and
 * pseudo-infinities, which the FPU itself rejects) are read by value with the
 * integer bit ignored for the class.
 */
#include "runtime-internal.h"

/* ---------------- IEEE interchange formats ---------------- */

/* the width of the exponent field whose bias is MAX_EXPONENT (2^(w-1) - 1) */
static int exponent_bits_of(int max_exponent)
{
	int bits = 1;
	for (; max_exponent; max_exponent >>= 1)
		bits++;
	return bits;
}

static int ieee_unpack(vccrt_u128 bits, int fraction_bits, int max_exponent, vccrt_unpacked *u)
{
	int biased_max = 2 * max_exponent + 1;
	vccrt_u128 fraction = bits & (((vccrt_u128)1 << fraction_bits) - 1);
	int biased = (int)(bits >> fraction_bits) & biased_max;
	u->negative = (int)(bits >> (fraction_bits + exponent_bits_of(max_exponent))) & 1;
	u->sticky = 0;
	if (biased == biased_max)
		return fraction ? 3 : 2;
	if (biased == 0) {
		int zeros;
		if (!fraction)
			return 1;
		zeros = vccrt_clz128(fraction);
		u->significand = fraction << zeros;
		u->exponent = 128 - zeros - max_exponent - fraction_bits;
		return 0;
	}
	u->significand = (fraction | ((vccrt_u128)1 << fraction_bits)) << (127 - fraction_bits);
	u->exponent = biased - max_exponent;
	return 0;
}

static vccrt_u128 ieee_pack(int negative, vccrt_rounded r, int fraction_bits, int max_exponent)
{
	int exponent_bits = exponent_bits_of(max_exponent);
	vccrt_u128 bits = (vccrt_u128)(negative != 0) << (fraction_bits + exponent_bits);
	bits |= (vccrt_u128)r.biased_exponent << fraction_bits;
	return bits | (r.significand & (((vccrt_u128)1 << fraction_bits) - 1));
}

static vccrt_nan ieee_nan(vccrt_u128 bits, int fraction_bits, int max_exponent)
{
	vccrt_nan n;
	int exponent_bits = exponent_bits_of(max_exponent);
	n.negative = (int)(bits >> (fraction_bits + exponent_bits)) & 1;
	n.quiet = (int)(bits >> (fraction_bits - 1)) & 1;
	/* the payload below the quiet bit, left-aligned */
	n.payload = (bits & (((vccrt_u128)1 << (fraction_bits - 1)) - 1)) << (129 - fraction_bits);
	return n;
}

static vccrt_u128 ieee_from_nan(vccrt_nan n, int fraction_bits, int max_exponent)
{
	int exponent_bits = exponent_bits_of(max_exponent);
	vccrt_u128 bits = (vccrt_u128)(n.negative != 0) << (fraction_bits + exponent_bits);
	vccrt_u128 payload = n.payload >> (129 - fraction_bits);
	bits |= (vccrt_u128)(2 * max_exponent + 1) << fraction_bits;
	if (n.quiet || !payload)	/* a NaN needs a nonzero fraction */
		bits |= (vccrt_u128)1 << (fraction_bits - 1);
	return bits | payload;
}

int __vccrt_float_unpack(float a, vccrt_unpacked *u)
{
	vccrt_float_bits b = { .value = a };
	return ieee_unpack(b.bits, 23, 127, u);
}

int __vccrt_double_unpack(double a, vccrt_unpacked *u)
{
	vccrt_double_bits b = { .value = a };
	return ieee_unpack(b.bits, 52, 1023, u);
}

int __vccrt_quad_unpack(__float128 a, vccrt_unpacked *u)
{
	vccrt_quad_bits b = { .value = a };
	return ieee_unpack(b.words.all, 112, 16383, u);
}

int __vccrt_small_unpack(vccrt_u16 bits, vccrt_format f, vccrt_unpacked *u)
{
	return ieee_unpack(bits, f.precision - 1, f.max_exponent, u);
}

float __vccrt_float_pack(int negative, vccrt_rounded r)
{
	vccrt_float_bits b;
	b.bits = (vccrt_u32)ieee_pack(negative, r, 23, 127);
	return b.value;
}

double __vccrt_double_pack(int negative, vccrt_rounded r)
{
	vccrt_double_bits b;
	b.bits = (vccrt_u64)ieee_pack(negative, r, 52, 1023);
	return b.value;
}

__float128 __vccrt_quad_pack(int negative, vccrt_rounded r)
{
	vccrt_quad_bits b;
	b.words.all = ieee_pack(negative, r, 112, 16383);
	return b.value;
}

vccrt_u16 __vccrt_small_pack(int negative, vccrt_rounded r, vccrt_format f)
{
	return (vccrt_u16)ieee_pack(negative, r, f.precision - 1, f.max_exponent);
}

vccrt_nan __vccrt_float_nan(float a)
{
	vccrt_float_bits b = { .value = a };
	return ieee_nan(b.bits, 23, 127);
}

vccrt_nan __vccrt_double_nan(double a)
{
	vccrt_double_bits b = { .value = a };
	return ieee_nan(b.bits, 52, 1023);
}

vccrt_nan __vccrt_quad_nan(__float128 a)
{
	vccrt_quad_bits b = { .value = a };
	return ieee_nan(b.words.all, 112, 16383);
}

vccrt_nan __vccrt_small_nan(vccrt_u16 bits, vccrt_format f)
{
	return ieee_nan(bits, f.precision - 1, f.max_exponent);
}

float __vccrt_float_from_nan(vccrt_nan n)
{
	vccrt_float_bits b;
	b.bits = (vccrt_u32)ieee_from_nan(n, 23, 127);
	return b.value;
}

double __vccrt_double_from_nan(vccrt_nan n)
{
	vccrt_double_bits b;
	b.bits = (vccrt_u64)ieee_from_nan(n, 52, 1023);
	return b.value;
}

__float128 __vccrt_quad_from_nan(vccrt_nan n)
{
	vccrt_quad_bits b;
	b.words.all = ieee_from_nan(n, 112, 16383);
	return b.value;
}

vccrt_u16 __vccrt_small_from_nan(vccrt_nan n, vccrt_format f)
{
	return (vccrt_u16)ieee_from_nan(n, f.precision - 1, f.max_exponent);
}

float __vccrt_float_special(int negative, int infinity)
{
	vccrt_float_bits b;
	b.bits = (vccrt_u32)(negative != 0) << 31 | (infinity ? 0x7f800000u : 0);
	return b.value;
}

double __vccrt_double_special(int negative, int infinity)
{
	vccrt_double_bits b;
	b.bits = (vccrt_u64)(negative != 0) << 63 | (infinity ? 0x7ff0000000000000ull : 0);
	return b.value;
}

__float128 __vccrt_quad_special(int negative, int infinity)
{
	vccrt_quad_bits b;
	b.words.half.low = 0;
	b.words.half.high = (vccrt_u64)(negative != 0) << 63 | (infinity ? 0x7fff000000000000ull : 0);
	return b.value;
}

/* ---------------- x87 extended ---------------- */

int __vccrt_x87_unpack(const long double *a, vccrt_unpacked *u)
{
	vccrt_x87_bits b;
	int biased, zeros;
	b.value = *a;
	biased = b.parts.sign_exponent & 0x7fff;
	u->negative = b.parts.sign_exponent >> 15;
	u->sticky = 0;
	if (biased == 0x7fff)
		return b.parts.significand << 1 ? 3 : 2;
	if (!b.parts.significand)
		return 1;
	if (!biased)
		biased = 1;	/* a subnormal (or pseudo-subnormal) has the smallest exponent */
	zeros = vccrt_clz64(b.parts.significand);
	u->significand = (vccrt_u128)(b.parts.significand << zeros) << 64;
	u->exponent = biased - 16383 - zeros;
	return 0;
}

long double __vccrt_x87_pack(int negative, vccrt_rounded r)
{
	vccrt_x87_bits b;
	b.value = 0;
	if (r.biased_exponent == 2 * 16383 + 1)
		r.significand = (vccrt_u128)1 << 63;	/* infinity has the integer bit */
	b.parts.significand = (vccrt_u64)r.significand;
	b.parts.sign_exponent = (vccrt_u16)((negative != 0) << 15 | r.biased_exponent);
	return b.value;
}

vccrt_nan __vccrt_x87_nan(const long double *a)
{
	vccrt_x87_bits b;
	vccrt_nan n;
	b.value = *a;
	n.negative = b.parts.sign_exponent >> 15;
	n.quiet = (int)(b.parts.significand >> 62) & 1;
	n.payload = (vccrt_u128)(b.parts.significand << 2) << 64;
	return n;
}

long double __vccrt_x87_from_nan(vccrt_nan n)
{
	vccrt_x87_bits b;
	vccrt_u64 payload = (vccrt_u64)(n.payload >> 66);
	b.value = 0;
	b.parts.significand = 1ull << 63 | payload;
	if (n.quiet || !payload)
		b.parts.significand |= 1ull << 62;
	b.parts.sign_exponent = (vccrt_u16)((n.negative != 0) << 15 | 0x7fff);
	return b.value;
}

long double __vccrt_x87_special(int negative, int infinity)
{
	vccrt_x87_bits b;
	b.value = 0;
	b.parts.significand = infinity ? 1ull << 63 : 0;
	b.parts.sign_exponent = (vccrt_u16)((negative != 0) << 15 | (infinity ? 0x7fff : 0));
	return b.value;
}
