/*
 * A floating value raised to an int power, libgcc's __builtin_powi
 * helpers: __powisf2 (float), __powidf2 (double), __powixf2 (long double),
 * __powitf2 (__float128).
 *
 * a^b by binary exponentiation: walk b's bits from the lowest, squaring a
 * at each step and multiplying the result by the current square where the
 * bit is set, all in the type of a; a negative b gives 1 / a^|b|.  The
 * result is not correctly rounded (each multiplication rounds), which is
 * what __builtin_powi promises: it may be less accurate than pow().
 */
#include "runtime-internal.h"

#define INTEGER_POWER(NAME, T)                                          \
	T NAME(T a, int b)                                              \
	{                                                               \
		unsigned n = b < 0 ? 0u - (unsigned)b : (unsigned)b;    \
		T result = (n & 1) ? a : (T)1;                          \
		while (n >>= 1) {                                       \
			a *= a;                                         \
			if (n & 1)                                      \
				result *= a;                            \
		}                                                       \
		return b < 0 ? (T)1 / result : result;                  \
	}

INTEGER_POWER(__powisf2, float)
INTEGER_POWER(__powidf2, double)
INTEGER_POWER(__powixf2, long double)
INTEGER_POWER(__powitf2, __float128)
