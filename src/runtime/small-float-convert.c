/*
 * Conversions to and from the 16-bit floating types: _Float16 (IEEE
 * binary16, "hf") and __bf16 (bfloat16, "bf": binary32's exponent with 8
 * bits of precision), libgcc's names:
 *
 *   __extendhfsf2  _Float16 -> float        __extendbfsf2  __bf16 -> float (exact)
 *   __truncsfhf2 __truncdfhf2 __truncxfhf2 __trunctfhf2   float, double, long double, __float128 -> _Float16
 *   __truncsfbf2 __truncdfbf2 __truncxfbf2 __trunctfbf2   ... -> __bf16
 *
 * Each narrowing rounds once, from the source value directly (never through
 * float: that would round twice), to nearest, ties to even, with gradual
 * underflow and overflow to infinity.  NaNs as quad-convert.c: the payload's
 * top bits kept; narrowing sets the quiet bit, except __truncsfbf2, which
 * keeps a signalling NaN signalling when the payload bits it drops are all
 * zero (the usual runtime's choice, as for __trunctfxf2); widening keeps it.
 * The 16-bit values travel in xmm0, as _Float16 and __bf16 do in the C ABI.
 */
#include "runtime-internal.h"

static float widen(vccrt_u16 bits, vccrt_format f)
{
	vccrt_unpacked u;
	int kind = __vccrt_small_unpack(bits, f, &u);
	if (kind == 3)
		return __vccrt_float_from_nan(__vccrt_small_nan(bits, f));
	if (kind)
		return __vccrt_float_special(u.negative, kind == 2);
	return __vccrt_float_pack(u.negative, __vccrt_round(u, VCCRT_FLOAT));
}

/* KIND, U and N from the source's unpacking; QUIET: a NaN is made quiet */
static vccrt_u16 narrow(int kind, vccrt_unpacked u, vccrt_nan n, vccrt_format f, int quiet)
{
	vccrt_rounded zero_or_infinity;
	if (kind == 3) {
		n.quiet |= quiet;
		return __vccrt_small_from_nan(n, f);
	}
	if (kind) {
		zero_or_infinity.biased_exponent = kind == 2 ? 2 * f.max_exponent + 1 : 0;
		zero_or_infinity.significand = 0;
		return __vccrt_small_pack(u.negative, zero_or_infinity, f);
	}
	return __vccrt_small_pack(u.negative, __vccrt_round(u, f), f);
}

float __extendhfsf2(_Float16 a)
{
	vccrt_half_bits b = { .value = a };
	return widen(b.bits, VCCRT_HALF);
}

float __extendbfsf2(__bf16 a)
{
	vccrt_bfloat_bits b = { .value = a };
	return widen(b.bits, VCCRT_BFLOAT);
}

static vccrt_u16 narrow_float(float a, vccrt_format f, int quiet)
{
	vccrt_unpacked u;
	int kind = __vccrt_float_unpack(a, &u);
	return narrow(kind, u, kind == 3 ? __vccrt_float_nan(a) : (vccrt_nan){ 0 }, f, quiet);
}

static vccrt_u16 narrow_double(double a, vccrt_format f)
{
	vccrt_unpacked u;
	int kind = __vccrt_double_unpack(a, &u);
	return narrow(kind, u, kind == 3 ? __vccrt_double_nan(a) : (vccrt_nan){ 0 }, f, 1);
}

static vccrt_u16 narrow_x87(const long double *a, vccrt_format f)
{
	vccrt_unpacked u;
	int kind = __vccrt_x87_unpack(a, &u);
	return narrow(kind, u, kind == 3 ? __vccrt_x87_nan(a) : (vccrt_nan){ 0 }, f, 1);
}

static vccrt_u16 narrow_quad(__float128 a, vccrt_format f)
{
	vccrt_unpacked u;
	int kind = __vccrt_quad_unpack(a, &u);
	return narrow(kind, u, kind == 3 ? __vccrt_quad_nan(a) : (vccrt_nan){ 0 }, f, 1);
}

static _Float16 half_from(vccrt_u16 bits)
{
	vccrt_half_bits b = { .bits = bits };
	return b.value;
}

static __bf16 bfloat_from(vccrt_u16 bits)
{
	vccrt_bfloat_bits b = { .bits = bits };
	return b.value;
}

_Float16 __truncsfhf2(float a)
{
	return half_from(narrow_float(a, VCCRT_HALF, 1));
}

_Float16 __truncdfhf2(double a)
{
	return half_from(narrow_double(a, VCCRT_HALF));
}

_Float16 __truncxfhf2(long double a)
{
	return half_from(narrow_x87(&a, VCCRT_HALF));
}

_Float16 __trunctfhf2(__float128 a)
{
	return half_from(narrow_quad(a, VCCRT_HALF));
}

__bf16 __truncsfbf2(float a)
{
	vccrt_nan n = __vccrt_float_nan(a);
	/* bfloat16 keeps the top 6 of float's 22 payload bits */
	return bfloat_from(narrow_float(a, VCCRT_BFLOAT, (n.payload << 6) != 0));
}

__bf16 __truncdfbf2(double a)
{
	return bfloat_from(narrow_double(a, VCCRT_BFLOAT));
}

__bf16 __truncxfbf2(long double a)
{
	return bfloat_from(narrow_x87(&a, VCCRT_BFLOAT));
}

__bf16 __trunctfbf2(__float128 a)
{
	return bfloat_from(narrow_quad(a, VCCRT_BFLOAT));
}
