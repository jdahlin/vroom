/* isnan, isinf, isfinite, isnormal, isinf_sign, fpclassify, isgreater ...
   isunordered and signbit on run-time values of every float type (zero,
   subnormal, normal, inf, NaN, both signs).  The x86-64 walker builds them
   on the bit patterns (x87: explicit integer bit; __float128: low word as a
   sticky bit); elsewhere only the forms the arm64 emitter has. */
#include <stdio.h>

static unsigned long h;
static void H(long v) { h = h * 1000003u + (unsigned long)v; }

#define CLS(T, x) do { volatile T v_ = (x); T v = v_; \
	H(__builtin_isnan(v)); H(__builtin_isinf(v)); H(__builtin_isfinite(v)); H(__builtin_isnormal(v)); \
	H(__builtin_signbit(v) != 0); X86(H(__builtin_isinf_sign(v)); \
	H(__builtin_fpclassify(10, 20, 30, 40, 50, v));) } while (0)
#define CMP(T, a, b) do { volatile T a_ = (a), b_ = (b); T x = a_, y = b_; \
	X86(H(__builtin_isgreater(x, y)); H(__builtin_isgreaterequal(x, y)); H(__builtin_isless(x, y)); \
	H(__builtin_islessequal(x, y)); H(__builtin_islessgreater(x, y)); H(__builtin_isunordered(x, y));) } while (0)
#ifdef __x86_64__
#define X86(...) __VA_ARGS__
#else
#define X86(...)
#endif

#define ALL(T, tiny) do { T z = 0, one = 1, inf = __builtin_inf(), nan = __builtin_nan(""); \
	CLS(T, z); CLS(T, -z); CLS(T, one); CLS(T, -one); CLS(T, tiny); CLS(T, -tiny); CLS(T, inf); CLS(T, -inf); \
	CLS(T, nan); CLS(T, -nan); CMP(T, one, z); CMP(T, z, one); CMP(T, one, one); CMP(T, nan, one); CMP(T, inf, nan); \
	CMP(T, -inf, inf); } while (0)

int main(void)
{
	ALL(float, 1e-40f);
	ALL(double, 1e-310);
	ALL(long double, 1e-4940L);
#ifdef __SIZEOF_FLOAT128__
	ALL(__float128, 1e-4940Q);
#endif
	printf("%lx\n", h);
	return 0;
}
