/*
 * Conversions between 128-bit integers and float, double and x87 long
 * double, libgcc's "Soft float library routines":
 *
 *   __floattisf __floattidf __floattixf        __int128 -> float, double, long double
 *   __floatuntisf __floatuntidf __floatuntixf  unsigned __int128 -> ...
 *   __fixsfti __fixdfti __fixxfti              float, double, long double -> __int128
 *   __fixunssfti __fixunsdfti __fixunsxfti     ... -> unsigned __int128
 *
 * To floating: rounded to nearest, ties to even (a 128-bit integer has more
 * bits than any of these significands).  From floating: truncated toward
 * zero; out of range saturates as __vccrt_integer_from says.  The values are
 * built and taken apart on their bits: a compiler lowers these conversions
 * to these very functions.
 */
#include "runtime-internal.h"

static vccrt_unpacked from_signed(vccrt_i128 a)
{
	vccrt_words w = { .signed_all = a };
	int negative = (vccrt_i64)w.half.high < 0;
	return __vccrt_integer_unpack(negative, negative ? (vccrt_u128)0 - w.all : w.all);
}

float __floattisf(vccrt_i128 a)
{
	vccrt_unpacked u;
	if (!a)
		return 0;
	u = from_signed(a);
	return __vccrt_float_pack(u.negative, __vccrt_round(u, VCCRT_FLOAT));
}

double __floattidf(vccrt_i128 a)
{
	vccrt_unpacked u;
	if (!a)
		return 0;
	u = from_signed(a);
	return __vccrt_double_pack(u.negative, __vccrt_round(u, VCCRT_DOUBLE));
}

long double __floattixf(vccrt_i128 a)
{
	vccrt_unpacked u;
	if (!a)
		return __vccrt_x87_special(0, 0);
	u = from_signed(a);
	return __vccrt_x87_pack(u.negative, __vccrt_round(u, VCCRT_X87));
}

float __floatuntisf(vccrt_u128 a)
{
	if (!a)
		return 0;
	return __vccrt_float_pack(0, __vccrt_round(__vccrt_integer_unpack(0, a), VCCRT_FLOAT));
}

double __floatuntidf(vccrt_u128 a)
{
	if (!a)
		return 0;
	return __vccrt_double_pack(0, __vccrt_round(__vccrt_integer_unpack(0, a), VCCRT_DOUBLE));
}

long double __floatuntixf(vccrt_u128 a)
{
	if (!a)
		return __vccrt_x87_special(0, 0);
	return __vccrt_x87_pack(0, __vccrt_round(__vccrt_integer_unpack(0, a), VCCRT_X87));
}

vccrt_i128 __fixsfti(float a)
{
	vccrt_unpacked u;
	int kind = __vccrt_float_unpack(a, &u);
	return (vccrt_i128)__vccrt_integer_from(kind, &u, 128, 1);
}

vccrt_i128 __fixdfti(double a)
{
	vccrt_unpacked u;
	int kind = __vccrt_double_unpack(a, &u);
	return (vccrt_i128)__vccrt_integer_from(kind, &u, 128, 1);
}

vccrt_i128 __fixxfti(long double a)
{
	vccrt_unpacked u;
	int kind = __vccrt_x87_unpack(&a, &u);
	return (vccrt_i128)__vccrt_integer_from(kind, &u, 128, 1);
}

vccrt_u128 __fixunssfti(float a)
{
	vccrt_unpacked u;
	int kind = __vccrt_float_unpack(a, &u);
	return __vccrt_integer_from(kind, &u, 128, 0);
}

vccrt_u128 __fixunsdfti(double a)
{
	vccrt_unpacked u;
	int kind = __vccrt_double_unpack(a, &u);
	return __vccrt_integer_from(kind, &u, 128, 0);
}

vccrt_u128 __fixunsxfti(long double a)
{
	vccrt_unpacked u;
	int kind = __vccrt_x87_unpack(&a, &u);
	return __vccrt_integer_from(kind, &u, 128, 0);
}
