/* A jump back from the second site must still read the original live
 * values at the first site. Both branches load from mutable storage. */
#if defined(__x86_64__) && defined(__linux__)
#include <setjmp.h>

extern int checkpoint(jmp_buf) __asm__("_setjmp") __attribute__((returns_twice));
static jmp_buf outer, inner;
static volatile int source = 1;

__attribute__((noinline)) static int resume(void)
{
	int a = source++, b = source++, c = source++, d = source++;
	int e = source++, f = source++, g = source++, h = source++;
	volatile int visits = 0;

	if (checkpoint(outer))
		return a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g + 8*h + visits;

	int i = source++, j = source++, k = source++, l = source++;
	int m = source++, n = source++, o = source++, p = source++;
	if (!checkpoint(inner)) {
		visits++;
		longjmp(inner, 1);
	}
	visits += i + j + k + l + m + n + o + p;
	longjmp(outer, 1);
}
#endif

int main(void)
{
#if defined(__x86_64__) && defined(__linux__)
	return resume() != 305;
#else
	return 0;
#endif
}
