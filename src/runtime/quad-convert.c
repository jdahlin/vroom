/*
 * Conversions to and from __float128 (IEEE binary128), libgcc's "Soft float
 * library routines":
 *
 *   __extendsftf2 __extenddftf2 __extendxftf2   float, double, long double -> __float128 (exact)
 *   __trunctfsf2 __trunctfdf2 __trunctfxf2      __float128 -> float, double, long double (rounded)
 *   __floatsitf __floatditf __floattitf          int, long long, __int128 -> __float128
 *   __floatunsitf __floatunditf __floatuntitf    the unsigned ones -> __float128
 *   __fixtfsi __fixtfdi __fixtfti                __float128 -> int, long long, __int128
 *   __fixunstfsi __fixunstfdi __fixunstfti       __float128 -> the unsigned ones
 *
 * Narrowing rounds to nearest, ties to even, with gradual underflow and
 * overflow to infinity.  A NaN keeps its sign and the top bits of its
 * payload; widening keeps the quiet bit as it is (a signalling NaN stays
 * signalling), narrowing sets it, except __trunctfxf2, which keeps a
 * signalling NaN signalling when the payload bits it drops are all zero
 * (so that the whole payload survives; the usual runtime's choices).  To an integer: truncated toward zero,
 * out of range saturates as __vccrt_integer_from says.  An int or long long
 * fits exactly; an __int128 is rounded.
 */
#include "runtime-internal.h"

__float128 __extendsftf2(float a)
{
	vccrt_unpacked u;
	int kind = __vccrt_float_unpack(a, &u);
	if (kind == 3)
		return __vccrt_quad_from_nan(__vccrt_float_nan(a));
	if (kind)
		return __vccrt_quad_special(u.negative, kind == 2);
	return __vccrt_quad_pack(u.negative, __vccrt_round(u, VCCRT_QUAD));
}

__float128 __extenddftf2(double a)
{
	vccrt_unpacked u;
	int kind = __vccrt_double_unpack(a, &u);
	if (kind == 3)
		return __vccrt_quad_from_nan(__vccrt_double_nan(a));
	if (kind)
		return __vccrt_quad_special(u.negative, kind == 2);
	return __vccrt_quad_pack(u.negative, __vccrt_round(u, VCCRT_QUAD));
}

__float128 __extendxftf2(long double a)
{
	vccrt_unpacked u;
	int kind = __vccrt_x87_unpack(&a, &u);
	if (kind == 3)
		return __vccrt_quad_from_nan(__vccrt_x87_nan(&a));
	if (kind)
		return __vccrt_quad_special(u.negative, kind == 2);
	return __vccrt_quad_pack(u.negative, __vccrt_round(u, VCCRT_QUAD));
}

float __trunctfsf2(__float128 a)
{
	vccrt_unpacked u;
	int kind = __vccrt_quad_unpack(a, &u);
	if (kind == 3) {
		vccrt_nan n = __vccrt_quad_nan(a);
		n.quiet = 1;
		return __vccrt_float_from_nan(n);
	}
	if (kind)
		return __vccrt_float_special(u.negative, kind == 2);
	return __vccrt_float_pack(u.negative, __vccrt_round(u, VCCRT_FLOAT));
}

double __trunctfdf2(__float128 a)
{
	vccrt_unpacked u;
	int kind = __vccrt_quad_unpack(a, &u);
	if (kind == 3) {
		vccrt_nan n = __vccrt_quad_nan(a);
		n.quiet = 1;
		return __vccrt_double_from_nan(n);
	}
	if (kind)
		return __vccrt_double_special(u.negative, kind == 2);
	return __vccrt_double_pack(u.negative, __vccrt_round(u, VCCRT_DOUBLE));
}

long double __trunctfxf2(__float128 a)
{
	vccrt_unpacked u;
	int kind = __vccrt_quad_unpack(a, &u);
	if (kind == 3) {
		vccrt_nan n = __vccrt_quad_nan(a);
		n.quiet |= (n.payload << 62) != 0;	/* x87 keeps the top 62 of 111 payload bits */
		return __vccrt_x87_from_nan(n);
	}
	if (kind)
		return __vccrt_x87_special(u.negative, kind == 2);
	return __vccrt_x87_pack(u.negative, __vccrt_round(u, VCCRT_X87));
}

static __float128 from_integer(int negative, vccrt_u128 magnitude)
{
	if (!magnitude)
		return __vccrt_quad_special(0, 0);
	return __vccrt_quad_pack(negative, __vccrt_round(__vccrt_integer_unpack(negative, magnitude), VCCRT_QUAD));
}

__float128 __floatsitf(int a)
{
	return from_integer(a < 0, a < 0 ? (vccrt_u128)(0u - (unsigned)a) : (vccrt_u128)(unsigned)a);
}

__float128 __floatditf(long long a)
{
	return from_integer(a < 0, a < 0 ? (vccrt_u128)(0ull - (vccrt_u64)a) : (vccrt_u128)(vccrt_u64)a);
}

__float128 __floattitf(vccrt_i128 a)
{
	vccrt_words w = { .signed_all = a };
	int negative = (vccrt_i64)w.half.high < 0;
	return from_integer(negative, negative ? (vccrt_u128)0 - w.all : w.all);
}

__float128 __floatunsitf(unsigned a)
{
	return from_integer(0, a);
}

__float128 __floatunditf(unsigned long long a)
{
	return from_integer(0, a);
}

__float128 __floatuntitf(vccrt_u128 a)
{
	return from_integer(0, a);
}

static vccrt_u128 to_integer(__float128 a, int bits, int is_signed)
{
	vccrt_unpacked u;
	int kind = __vccrt_quad_unpack(a, &u);
	return __vccrt_integer_from(kind, &u, bits, is_signed);
}

int __fixtfsi(__float128 a)
{
	return (int)(unsigned)to_integer(a, 32, 1);
}

long long __fixtfdi(__float128 a)
{
	return (long long)(vccrt_u64)to_integer(a, 64, 1);
}

vccrt_i128 __fixtfti(__float128 a)
{
	return (vccrt_i128)to_integer(a, 128, 1);
}

unsigned __fixunstfsi(__float128 a)
{
	return (unsigned)to_integer(a, 32, 0);
}

unsigned long long __fixunstfdi(__float128 a)
{
	return (vccrt_u64)to_integer(a, 64, 0);
}

vccrt_u128 __fixunstfti(__float128 a)
{
	return to_integer(a, 128, 0);
}
