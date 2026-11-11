/* floor(double(float)) can use floorf without losing result precision.
 * A weak double implementation detects missed narrowing, as GCC's torture test. */
extern void abort(void);
extern double floor(double);
__attribute__((noinline)) double rounded(float x) { return floor(x); }
__attribute__((noinline)) float rounded_float(float x) { return floor(x); }
__attribute__((weak)) double floor(double x) { abort(); }
__attribute__((weak)) float floorf(float x)
{
	if (x == 0.0f) return x;
	int i = (int)x;
	return (float)(i - (x < (float)i));
}
#define ROUNDING(name, adjust) \
extern double name(double); \
__attribute__((noinline)) double call_##name(float x) { return name(x); } \
__attribute__((weak)) double name(double x) { abort(); } \
__attribute__((weak)) float name##f(float x) { \
	if (x == 0.0f) return x; \
	int i = (int)x; return (float)(adjust); \
}
ROUNDING(ceil, i + (x > (float)i))
ROUNDING(trunc, i)
ROUNDING(round, i + (x - (float)i >= 0.5f) - ((float)i - x >= 0.5f))
int main(void)
{
#ifdef __OPTIMIZE__
	volatile float x = -1.25f;
	if (rounded(x) != -2.0 || rounded_float(x) != -2.0f) abort();
	if (call_ceil(x) != -1.0 || call_trunc(x) != -1.0 || call_round(x) != -1.0) abort();
	x = -1.75f;
	if (call_round(x) != -2.0) abort();
	x = -0.0f;
	if (!__builtin_signbit(rounded(x))) abort();
#endif
	return 0;
}
