/*
 * Complex multiplication and division with C11 Annex G's treatment of
 * infinities and NaNs, libgcc's names:
 *
 *   __mulsc3 __muldc3 __mulxc3 __multc3   (a + ib) * (c + id), float, double, long double, __float128
 *   __divsc3 __divdc3 __divxc3 __divtc3   (a + ib) / (c + id)
 *
 * Both follow the example implementations of C11 G.5.1 (_Cmultd, _Cdivd):
 * the textbook formula first, and only when both parts come out NaN, the
 * recovery of the infinite or zero result the operands imply (an infinite
 * operand times a nonzero finite one is infinite, a finite value divided by
 * an infinite one is zero, a nonzero value divided by zero is infinite).
 * Division scales the divisor by a power of two (logb/scalbn) so that c*c +
 * d*d neither overflows nor underflows.  The arithmetic is in the operands'
 * type with no contraction into fused multiply-adds (build.sh compiles with
 * -ffp-contract=off); for __float128 it is the library's own soft float.
 *
 * One departure from the example's text: its finite / infinite case tests
 * only that logb of the divisor's larger part is infinite, which is also
 * true (-inf) when one part is zero and the other a NaN, and would turn
 * that NaN quotient into zero.  The case here requires +inf, an infinite
 * divisor, as the example's comment says it means.
 *
 * One definition per type from a macro: the four copies differ only in the
 * type and its math functions (real-math.c).
 */
#include "runtime-internal.h"

#define COMPLEX_MULTIPLY(NAME, T, IS_NAN, IS_INF, COPYSIGN, INFINITY_VALUE)                         \
	_Complex T NAME(T a, T b, T c, T d)                                                            \
	{                                                                                              \
		union {                                                                                \
			_Complex T value;                                                              \
			T parts[2];                                                                    \
		} result;                                                                              \
		T ac = a * c, bd = b * d, ad = a * d, bc = b * c;                                      \
		T x = ac - bd, y = ad + bc;                                                            \
		if (IS_NAN(x) && IS_NAN(y)) {                                                          \
			int recalculate = 0;                                                           \
			if (IS_INF(a) || IS_INF(b)) { /* the first operand is infinite */               \
				a = COPYSIGN(IS_INF(a) ? (T)1 : (T)0, a);                              \
				b = COPYSIGN(IS_INF(b) ? (T)1 : (T)0, b);                              \
				if (IS_NAN(c))                                                         \
					c = COPYSIGN((T)0, c);                                         \
				if (IS_NAN(d))                                                         \
					d = COPYSIGN((T)0, d);                                         \
				recalculate = 1;                                                       \
			}                                                                              \
			if (IS_INF(c) || IS_INF(d)) { /* the second operand is infinite */              \
				c = COPYSIGN(IS_INF(c) ? (T)1 : (T)0, c);                              \
				d = COPYSIGN(IS_INF(d) ? (T)1 : (T)0, d);                              \
				if (IS_NAN(a))                                                         \
					a = COPYSIGN((T)0, a);                                         \
				if (IS_NAN(b))                                                         \
					b = COPYSIGN((T)0, b);                                         \
				recalculate = 1;                                                       \
			}                                                                              \
			if (!recalculate && (IS_INF(ac) || IS_INF(bd) || IS_INF(ad) || IS_INF(bc))) { \
				/* overflow in a partial product: recover infinities */                \
				if (IS_NAN(a))                                                         \
					a = COPYSIGN((T)0, a);                                         \
				if (IS_NAN(b))                                                         \
					b = COPYSIGN((T)0, b);                                         \
				if (IS_NAN(c))                                                         \
					c = COPYSIGN((T)0, c);                                         \
				if (IS_NAN(d))                                                         \
					d = COPYSIGN((T)0, d);                                         \
				recalculate = 1;                                                       \
			}                                                                              \
			if (recalculate) {                                                             \
				x = INFINITY_VALUE * (a * c - b * d);                                  \
				y = INFINITY_VALUE * (a * d + b * c);                                  \
			}                                                                              \
		}                                                                                      \
		result.parts[0] = x;                                                                   \
		result.parts[1] = y;                                                                   \
		return result.value;                                                                   \
	}

#define COMPLEX_DIVIDE(NAME, T, IS_NAN, IS_INF, COPYSIGN, LOGB, SCALBN, FABS, INFINITY_VALUE)       \
	_Complex T NAME(T a, T b, T c, T d)                                                            \
	{                                                                                              \
		union {                                                                                \
			_Complex T value;                                                              \
			T parts[2];                                                                    \
		} result;                                                                              \
		T abs_c = FABS(c), abs_d = FABS(d), larger, scale_exponent, denominator, x, y;       \
		int scale = 0;                                                                         \
		/* fmax: a NaN loses to a number */                                                    \
		larger = IS_NAN(abs_c) ? abs_d : IS_NAN(abs_d) ? abs_c : abs_c > abs_d ? abs_c : abs_d; \
		scale_exponent = LOGB(larger);                                                         \
		if (!IS_NAN(scale_exponent) && !IS_INF(scale_exponent)) {                              \
			scale = (int)scale_exponent;                                                   \
			c = SCALBN(c, -scale);                                                         \
			d = SCALBN(d, -scale);                                                         \
		}                                                                                      \
		denominator = c * c + d * d;                                                           \
		x = SCALBN((a * c + b * d) / denominator, -scale);                                     \
		y = SCALBN((b * c - a * d) / denominator, -scale);                                     \
		if (IS_NAN(x) && IS_NAN(y)) {                                                          \
			if (denominator == (T)0 && (!IS_NAN(a) || !IS_NAN(b))) {                       \
				/* nonzero / zero: infinite */                                         \
				x = COPYSIGN(INFINITY_VALUE, c) * a;                                   \
				y = COPYSIGN(INFINITY_VALUE, c) * b;                                   \
			} else if ((IS_INF(a) || IS_INF(b)) && !IS_INF(c) && !IS_NAN(c) && !IS_INF(d) && \
				   !IS_NAN(d)) {                                                       \
				/* infinite / finite: infinite */                                      \
				a = COPYSIGN(IS_INF(a) ? (T)1 : (T)0, a);                              \
				b = COPYSIGN(IS_INF(b) ? (T)1 : (T)0, b);                              \
				x = INFINITY_VALUE * (a * c + b * d);                                  \
				y = INFINITY_VALUE * (b * c - a * d);                                  \
			} else if (IS_INF(scale_exponent) && scale_exponent > (T)0 && !IS_INF(a) &&     \
				   !IS_NAN(a) && !IS_INF(b) && !IS_NAN(b)) {                           \
				/* finite / infinite: zero */                                          \
				c = COPYSIGN(IS_INF(c) ? (T)1 : (T)0, c);                              \
				d = COPYSIGN(IS_INF(d) ? (T)1 : (T)0, d);                              \
				x = (T)0 * (a * c + b * d);                                            \
				y = (T)0 * (b * c - a * d);                                            \
			}                                                                              \
		}                                                                                      \
		result.parts[0] = x;                                                                   \
		result.parts[1] = y;                                                                   \
		return result.value;                                                                   \
	}

static float float_fabs(float a)
{
	return __vccrt_float_copysign(a, 0.0f);
}

static double double_fabs(double a)
{
	return __vccrt_double_copysign(a, 0.0);
}

static long double x87_fabs(long double a)
{
	return __vccrt_x87_copysign(a, 0.0L);
}

COMPLEX_MULTIPLY(__mulsc3, float, __vccrt_float_is_nan, __vccrt_float_is_infinite, __vccrt_float_copysign,
		 __vccrt_float_special(0, 1))
COMPLEX_MULTIPLY(__muldc3, double, __vccrt_double_is_nan, __vccrt_double_is_infinite, __vccrt_double_copysign,
		 __vccrt_double_special(0, 1))
COMPLEX_MULTIPLY(__mulxc3, long double, __vccrt_x87_is_nan, __vccrt_x87_is_infinite, __vccrt_x87_copysign,
		 __vccrt_x87_special(0, 1))
COMPLEX_MULTIPLY(__multc3, __float128, __vccrt_quad_is_nan, __vccrt_quad_is_infinite, __vccrt_quad_copysign,
		 __vccrt_quad_special(0, 1))

COMPLEX_DIVIDE(__divsc3, float, __vccrt_float_is_nan, __vccrt_float_is_infinite, __vccrt_float_copysign,
	       __vccrt_float_logb, __vccrt_float_scalbn, float_fabs, __vccrt_float_special(0, 1))
COMPLEX_DIVIDE(__divdc3, double, __vccrt_double_is_nan, __vccrt_double_is_infinite, __vccrt_double_copysign,
	       __vccrt_double_logb, __vccrt_double_scalbn, double_fabs, __vccrt_double_special(0, 1))
COMPLEX_DIVIDE(__divxc3, long double, __vccrt_x87_is_nan, __vccrt_x87_is_infinite, __vccrt_x87_copysign,
	       __vccrt_x87_logb, __vccrt_x87_scalbn, x87_fabs, __vccrt_x87_special(0, 1))
COMPLEX_DIVIDE(__divtc3, __float128, __vccrt_quad_is_nan, __vccrt_quad_is_infinite, __vccrt_quad_copysign,
	       __vccrt_quad_logb, __vccrt_quad_scalbn, __vccrt_quad_fabs, __vccrt_quad_special(0, 1))
