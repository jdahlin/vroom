/* Integer-power builtins use compiler-runtime helpers, never these wrappers.
 * Supply the helper ABI for the freestanding test runtime as well as libc. */
#include <limits.h>
#define HELPER(T, NAME) \
T NAME(T x, int exponent) { \
  unsigned n = exponent < 0 ? 0u - (unsigned)exponent : (unsigned)exponent; \
  T result = 1; \
  while (n) { if (n & 1) result *= x; n >>= 1; if (n) x *= x; } \
  return exponent < 0 ? (T)1 / result : result; \
}
HELPER(float, __powisf2)
HELPER(double, __powidf2)
#if __LDBL_MANT_DIG__ == 64
HELPER(long double, __powixf2)
#else
HELPER(long double, __powitf2)
#endif
static volatile int base_calls, exponent_calls;
static double base(void) { base_calls++; return -2.; }
static int exponent(void) { exponent_calls++; return -3; }
float powif(float x, int n) { return __builtin_powif(x, n); }
double powi(double x, int n) { return __builtin_powi(x, n); }
long double powil(long double x, int n) { return __builtin_powil(x, n); }
int main(void)
{
  if (__builtin_powi(base(), exponent()) != -0.125 || base_calls != 1 || exponent_calls != 1) return 1;
  if (powif(2.f, 5) != 32.f || powi(2., -5) != .03125 || powil(2.L, -5) != .03125L) return 2;
  if (powif(-1.f, INT_MIN) != 1.f || powi(-1., INT_MIN) != 1. || powil(-1.L, INT_MIN) != 1.L) return 3;
  if (powif(-1.f, INT_MIN + 1) != -1.f || powi(-1., INT_MIN + 1) != -1. || powil(-1.L, INT_MIN + 1) != -1.L) return 4;
  if (powi(__builtin_nan(""), 0) != 1. || powil(__builtin_infl(), 0) != 1.L) return 5;
  if (!__builtin_signbit(powi(-0., 3)) || __builtin_signbit(powi(-0., 2))) return 6;
  if (!__builtin_isinf(powil(-0.L, -3)) || !__builtin_signbit(powil(-0.L, -3))) return 7;
  return 0;
}
