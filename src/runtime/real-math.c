/*
 * The few <math.h> functions the complex and power helpers need, for float,
 * double, x87 long double and __float128, on the bits so that the library
 * needs no libm: is a NaN, is infinite, copysign, logb and scalbn.
 *
 * logb(x) is the exponent of x's leading bit as a floating value (C11
 * 7.12.6.11): -inf for zero, +inf for an infinity, x for a NaN.
 * scalbn(x, n) is x * 2^n rounded once (to nearest, ties to even): the
 * value is unpacked exactly, its exponent moved and the result rounded by
 * __vccrt_round, so a subnormal result is correct without double rounding.
 * Zero, infinity and NaN come back unchanged.
 */
#include "runtime-internal.h"

/* n clamped so that exponent + n cannot overflow; any |n| beyond every
   format's range gives the same result */
static int clamp(int n)
{
	return n > 100000 ? 100000 : n < -100000 ? -100000 : n;
}

int __vccrt_float_is_nan(float a)
{
	vccrt_float_bits b = { .value = a };
	return (b.bits & 0x7fffffffu) > 0x7f800000u;
}

int __vccrt_float_is_infinite(float a)
{
	vccrt_float_bits b = { .value = a };
	return (b.bits & 0x7fffffffu) == 0x7f800000u;
}

float __vccrt_float_copysign(float a, float b)
{
	vccrt_float_bits x = { .value = a }, y = { .value = b };
	x.bits = (x.bits & 0x7fffffffu) | (y.bits & 0x80000000u);
	return x.value;
}

float __vccrt_float_logb(float a)
{
	vccrt_unpacked u;
	int kind = __vccrt_float_unpack(a, &u);
	if (kind == 1)
		return __vccrt_float_special(1, 1);
	if (kind == 2)
		return __vccrt_float_special(0, 1);
	return kind == 3 ? a : (float)u.exponent;
}

float __vccrt_float_scalbn(float a, int n)
{
	vccrt_unpacked u;
	if (__vccrt_float_unpack(a, &u))
		return a;
	u.exponent += clamp(n);
	return __vccrt_float_pack(u.negative, __vccrt_round(u, VCCRT_FLOAT));
}

int __vccrt_double_is_nan(double a)
{
	vccrt_double_bits b = { .value = a };
	return (b.bits & ~(1ull << 63)) > 0x7ff0000000000000ull;
}

int __vccrt_double_is_infinite(double a)
{
	vccrt_double_bits b = { .value = a };
	return (b.bits & ~(1ull << 63)) == 0x7ff0000000000000ull;
}

double __vccrt_double_copysign(double a, double b)
{
	vccrt_double_bits x = { .value = a }, y = { .value = b };
	x.bits = (x.bits & ~(1ull << 63)) | (y.bits & 1ull << 63);
	return x.value;
}

double __vccrt_double_logb(double a)
{
	vccrt_unpacked u;
	int kind = __vccrt_double_unpack(a, &u);
	if (kind == 1)
		return __vccrt_double_special(1, 1);
	if (kind == 2)
		return __vccrt_double_special(0, 1);
	return kind == 3 ? a : (double)u.exponent;
}

double __vccrt_double_scalbn(double a, int n)
{
	vccrt_unpacked u;
	if (__vccrt_double_unpack(a, &u))
		return a;
	u.exponent += clamp(n);
	return __vccrt_double_pack(u.negative, __vccrt_round(u, VCCRT_DOUBLE));
}

int __vccrt_x87_is_nan(long double a)
{
	vccrt_x87_bits b = { .value = a };
	return (b.parts.sign_exponent & 0x7fff) == 0x7fff && b.parts.significand << 1;
}

int __vccrt_x87_is_infinite(long double a)
{
	vccrt_x87_bits b = { .value = a };
	return (b.parts.sign_exponent & 0x7fff) == 0x7fff && !(b.parts.significand << 1);
}

long double __vccrt_x87_copysign(long double a, long double b)
{
	vccrt_x87_bits x = { .value = a }, y = { .value = b };
	x.parts.sign_exponent = (vccrt_u16)((x.parts.sign_exponent & 0x7fff) | (y.parts.sign_exponent & 0x8000));
	return x.value;
}

long double __vccrt_x87_logb(long double a)
{
	vccrt_unpacked u;
	int kind = __vccrt_x87_unpack(&a, &u);
	if (kind == 1)
		return __vccrt_x87_special(1, 1);
	if (kind == 2)
		return __vccrt_x87_special(0, 1);
	return kind == 3 ? a : (long double)u.exponent;
}

long double __vccrt_x87_scalbn(long double a, int n)
{
	vccrt_unpacked u;
	if (__vccrt_x87_unpack(&a, &u))
		return a;
	u.exponent += clamp(n);
	return __vccrt_x87_pack(u.negative, __vccrt_round(u, VCCRT_X87));
}

int __vccrt_quad_is_nan(__float128 a)
{
	vccrt_quad_bits b = { .value = a };
	return (b.words.all & ~((vccrt_u128)1 << 127)) > (vccrt_u128)0x7fff << 112;
}

int __vccrt_quad_is_infinite(__float128 a)
{
	vccrt_quad_bits b = { .value = a };
	return (b.words.all & ~((vccrt_u128)1 << 127)) == (vccrt_u128)0x7fff << 112;
}

__float128 __vccrt_quad_copysign(__float128 a, __float128 b)
{
	vccrt_quad_bits x = { .value = a }, y = { .value = b };
	x.words.half.high = (x.words.half.high & ~(1ull << 63)) | (y.words.half.high & 1ull << 63);
	return x.value;
}

__float128 __vccrt_quad_fabs(__float128 a)
{
	vccrt_quad_bits x = { .value = a };
	x.words.half.high &= ~(1ull << 63);
	return x.value;
}

/* (the exponent is an int: converting it is __floatsitf, exact) */
__float128 __vccrt_quad_logb(__float128 a)
{
	vccrt_unpacked u;
	int kind = __vccrt_quad_unpack(a, &u);
	if (kind == 1)
		return __vccrt_quad_special(1, 1);
	if (kind == 2)
		return __vccrt_quad_special(0, 1);
	return kind == 3 ? a : (__float128)u.exponent;
}

__float128 __vccrt_quad_scalbn(__float128 a, int n)
{
	vccrt_unpacked u;
	if (__vccrt_quad_unpack(a, &u))
		return a;
	u.exponent += clamp(n);
	return __vccrt_quad_pack(u.negative, __vccrt_round(u, VCCRT_QUAD));
}
