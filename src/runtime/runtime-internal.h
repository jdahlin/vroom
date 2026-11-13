/*
 * What the files of vcc's run-time support library share (src/runtime/,
 * libvccrt.a, doc/PLAN-win64.md step WIN7R): the helper functions code for
 * x86_64-w64-mingw32 calls for operations the CPU has no instruction for
 * (__int128 division and shifts, conversions between __int128 and floating
 * types, __float128, _Float16 and __bf16 arithmetic and conversions,
 * complex multiplication and division, integer powers) and the stack probe.
 *
 * Each helper is written from its specification: the semantics GCC's
 * manual gives for the libgcc routine of that name ("Integer library
 * routines", "Soft float library routines"), IEEE 754 binary formats with
 * rounding to nearest, ties to even, and C11 Annex G for the complex ones.
 * Where the specification leaves a choice open (which NaN a NaN operand
 * gives, what an out-of-range conversion to an integer gives) the library
 * makes the choice the compilers' usual runtime makes, so that a program
 * behaves the same whichever runtime it is linked with; the comment at each
 * place says what the choice is.  The rounding mode is always to nearest
 * (the default; reading MXCSR needs an instruction C cannot express) and no
 * exception flags are raised.
 *
 * The entry points have the C ABI of the target, which is the ABI clang uses
 * when it calls them (checked with clang -S): __int128 and long double
 * arguments by reference, __int128 returned in xmm0, long double and
 * __float128 returned through a hidden pointer, _Float16 and __bf16 in xmm0.
 *
 * Rules that keep the library free of anything but itself, for any compiler
 * that builds it (clang today, vcc later; src/runtime/build.sh):
 *   - no operation that a compiler lowers to a call to one of these helpers
 *     inside the helper that implements it: integer-divide.c and
 *     integer-shift.c work on 64-bit halves (no __int128 division or
 *     variable shift), the conversions build floating values from bits;
 *     elsewhere __int128 shifts and __float128 arithmetic may call the
 *     library's own helpers;
 *   - no libc or libm: the few functions needed (logb, scalbn, isnan, ...)
 *     are here, on the bits;
 *   - plain C (GNU C11 with __int128, _Float16, __bf16, __float128): no
 *     intrinsics headers, no SIMD builtins, no inline asm.
 */
#ifndef VCCRT_RUNTIME_INTERNAL_H
#define VCCRT_RUNTIME_INTERNAL_H

typedef unsigned char vccrt_u8;
typedef unsigned short vccrt_u16;
typedef unsigned int vccrt_u32;
typedef int vccrt_i32;
typedef unsigned long long vccrt_u64;
typedef long long vccrt_i64;
typedef unsigned __int128 vccrt_u128;
typedef __int128 vccrt_i128;

/* the two 64-bit halves of a 128-bit integer, little-endian */
typedef union {
	vccrt_u128 all;
	vccrt_i128 signed_all;
	struct {
		vccrt_u64 low, high;
	} half;
} vccrt_words;

/* the bits of each floating type */
typedef union {
	float value;
	vccrt_u32 bits;
} vccrt_float_bits;
typedef union {
	double value;
	vccrt_u64 bits;
} vccrt_double_bits;
typedef union {
	long double value;	/* x87 extended: 64-bit significand with an explicit integer bit */
	struct {
		vccrt_u64 significand;
		vccrt_u16 sign_exponent;
	} parts;
} vccrt_x87_bits;
typedef union {
	__float128 value;
	vccrt_words words;
} vccrt_quad_bits;
typedef union {
	_Float16 value;
	vccrt_u16 bits;
} vccrt_half_bits;
typedef union {
	__bf16 value;
	vccrt_u16 bits;
} vccrt_bfloat_bits;

/* functions one file defines and others call: in the implementation
   namespace (COFF has no hidden visibility) */
#define VCCRT_HIDDEN
#define VCCRT_INLINE static inline __attribute__((always_inline))

/* leading zero bits of a nonzero 64-bit value */
VCCRT_INLINE int vccrt_clz64(vccrt_u64 x)
{
	return __builtin_clzll(x);
}

/* leading zero bits of a nonzero 128-bit value (two 64-bit halves: no
   variable 128-bit shift) */
VCCRT_INLINE int vccrt_clz128(vccrt_u128 x)
{
	vccrt_words w = { .all = x };
	return w.half.high ? vccrt_clz64(w.half.high) : 64 + vccrt_clz64(w.half.low);
}

/*
 * A finite nonzero value unpacked for rounding: value = significand *
 * 2^(exponent - 127), with bit 127 of significand set; sticky is nonzero
 * when bits below the significand were dropped (the value is slightly
 * larger in magnitude than significand says).
 */
typedef struct {
	int negative;
	int exponent;
	vccrt_u128 significand;
	int sticky;
} vccrt_unpacked;

/* A binary format: precision in bits including the leading one (11 for
   half, 8 for bfloat16, 24 float, 53 double, 64 x87, 113 quad) and the
   largest exponent (the bias). */
typedef struct {
	int precision;
	int max_exponent;
} vccrt_format;

#define VCCRT_HALF ((vccrt_format){ 11, 15 })
#define VCCRT_BFLOAT ((vccrt_format){ 8, 127 })
#define VCCRT_FLOAT ((vccrt_format){ 24, 127 })
#define VCCRT_DOUBLE ((vccrt_format){ 53, 1023 })
#define VCCRT_X87 ((vccrt_format){ 64, 16383 })
#define VCCRT_QUAD ((vccrt_format){ 113, 16383 })

/*
 * A value rounded to a format (to nearest, ties to even): biased_exponent is
 * 0 for a subnormal or zero, 2 * max_exponent + 1 for an overflow to
 * infinity; significand has precision bits, the leading one included (it is
 * clear for a subnormal).
 */
typedef struct {
	int biased_exponent;
	vccrt_u128 significand;
} vccrt_rounded;

vccrt_rounded __vccrt_round(vccrt_unpacked u, vccrt_format f) VCCRT_HIDDEN;

/* Unpacking: each returns 0 for a finite nonzero value (filled in), 1 for
   zero, 2 for infinity, 3 for NaN; negative is set in every case.  The
   quiet-NaN bit and payload are left to the caller. */
int __vccrt_float_unpack(float a, vccrt_unpacked *u) VCCRT_HIDDEN;
int __vccrt_double_unpack(double a, vccrt_unpacked *u) VCCRT_HIDDEN;
int __vccrt_x87_unpack(const long double *a, vccrt_unpacked *u) VCCRT_HIDDEN;
int __vccrt_quad_unpack(__float128 a, vccrt_unpacked *u) VCCRT_HIDDEN;
int __vccrt_small_unpack(vccrt_u16 bits, vccrt_format f, vccrt_unpacked *u) VCCRT_HIDDEN;	/* half, bfloat16 */

/* Packing a rounded value (finite, or overflowed to infinity) with a sign */
float __vccrt_float_pack(int negative, vccrt_rounded r) VCCRT_HIDDEN;
double __vccrt_double_pack(int negative, vccrt_rounded r) VCCRT_HIDDEN;
long double __vccrt_x87_pack(int negative, vccrt_rounded r) VCCRT_HIDDEN;
__float128 __vccrt_quad_pack(int negative, vccrt_rounded r) VCCRT_HIDDEN;
vccrt_u16 __vccrt_small_pack(int negative, vccrt_rounded r, vccrt_format f) VCCRT_HIDDEN;	/* half, bfloat16 */

/* A NaN carried to another format: the sign, the payload's top bits, and
   QUIET (the quiet bit set, or kept from the source) */
typedef struct {
	int negative;
	vccrt_u128 payload;	/* the fraction bits below the quiet bit, left-aligned at bit 127 */
	int quiet;
} vccrt_nan;

vccrt_nan __vccrt_float_nan(float a) VCCRT_HIDDEN;
vccrt_nan __vccrt_double_nan(double a) VCCRT_HIDDEN;
vccrt_nan __vccrt_x87_nan(const long double *a) VCCRT_HIDDEN;
vccrt_nan __vccrt_quad_nan(__float128 a) VCCRT_HIDDEN;
vccrt_nan __vccrt_small_nan(vccrt_u16 bits, vccrt_format f) VCCRT_HIDDEN;
float __vccrt_float_from_nan(vccrt_nan n) VCCRT_HIDDEN;
double __vccrt_double_from_nan(vccrt_nan n) VCCRT_HIDDEN;
long double __vccrt_x87_from_nan(vccrt_nan n) VCCRT_HIDDEN;
__float128 __vccrt_quad_from_nan(vccrt_nan n) VCCRT_HIDDEN;
vccrt_u16 __vccrt_small_from_nan(vccrt_nan n, vccrt_format f) VCCRT_HIDDEN;

/* infinity and zero of each format with a sign */
float __vccrt_float_special(int negative, int infinity) VCCRT_HIDDEN;
double __vccrt_double_special(int negative, int infinity) VCCRT_HIDDEN;
long double __vccrt_x87_special(int negative, int infinity) VCCRT_HIDDEN;
__float128 __vccrt_quad_special(int negative, int infinity) VCCRT_HIDDEN;

/* 128-bit integer <-> unpacked: the magnitude of a nonzero integer */
vccrt_unpacked __vccrt_integer_unpack(int negative, vccrt_u128 magnitude) VCCRT_HIDDEN;
/* an unpacked value (or its class from *_unpack) truncated toward zero to an
   integer of BITS bits (32, 64, 128), signed or not, saturating */
vccrt_u128 __vccrt_integer_from(int kind, const vccrt_unpacked *u, int bits, int is_signed) VCCRT_HIDDEN;

/* the <math.h> functions the complex helpers use, on the bits (real-math.c) */
int __vccrt_quad_is_nan(__float128 a) VCCRT_HIDDEN;
int __vccrt_quad_is_infinite(__float128 a) VCCRT_HIDDEN;
__float128 __vccrt_quad_copysign(__float128 a, __float128 b) VCCRT_HIDDEN;
__float128 __vccrt_quad_fabs(__float128 a) VCCRT_HIDDEN;
__float128 __vccrt_quad_logb(__float128 a) VCCRT_HIDDEN;
__float128 __vccrt_quad_scalbn(__float128 a, int n) VCCRT_HIDDEN;

/* the same for float, double and long double */
int __vccrt_float_is_nan(float a) VCCRT_HIDDEN;
int __vccrt_float_is_infinite(float a) VCCRT_HIDDEN;
float __vccrt_float_copysign(float a, float b) VCCRT_HIDDEN;
float __vccrt_float_logb(float a) VCCRT_HIDDEN;
float __vccrt_float_scalbn(float a, int n) VCCRT_HIDDEN;
int __vccrt_double_is_nan(double a) VCCRT_HIDDEN;
int __vccrt_double_is_infinite(double a) VCCRT_HIDDEN;
double __vccrt_double_copysign(double a, double b) VCCRT_HIDDEN;
double __vccrt_double_logb(double a) VCCRT_HIDDEN;
double __vccrt_double_scalbn(double a, int n) VCCRT_HIDDEN;
int __vccrt_x87_is_nan(long double a) VCCRT_HIDDEN;
int __vccrt_x87_is_infinite(long double a) VCCRT_HIDDEN;
long double __vccrt_x87_copysign(long double a, long double b) VCCRT_HIDDEN;
long double __vccrt_x87_logb(long double a) VCCRT_HIDDEN;
long double __vccrt_x87_scalbn(long double a, int n) VCCRT_HIDDEN;

#endif
