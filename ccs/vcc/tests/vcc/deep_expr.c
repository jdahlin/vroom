/* Right-nested expressions deeper than the temporary registers need
   spilling (TODO.md tier 3, "Register spilling"). */
/* vcc-xfail: Register spilling: ~18-deep right-nested expression is fatal */
#include <stdio.h>

volatile long a[64];
#define N2(e, i) a[i] + (e)
#define N8(e, i) N2(N2(N2(N2(N2(N2(N2(N2(e, i), i + 1), i + 2), i + 3), i + 4), i + 5), i + 6), i + 7)

__attribute__((noinline)) long deep(void)
{
	return N8(N8(N8(N8(a[32], 0), 8), 16), 24);	/* 33 deep */
}

#define M(e, i) x * (a[i] - (e))
#define M4(e, i) M(M(M(M(e, i), i + 1), i + 2), i + 3)

__attribute__((noinline)) long deepmul(long x)
{
	return M4(M4(M4(M4(M4(x * a[20], 0), 4), 8), 12), 16);	/* 21 deep, every level a live product */
}

int main(void)
{
	long s;

	for (int i = 0; i < 64; i++)
		a[i] = i + 1;
	s = deep();
	printf("%ld %ld\n", s, deepmul(2));
	return s != 33 * 34 / 2;
}
