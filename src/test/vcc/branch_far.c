/* vcc-ref-timeout: 300 */
/* Conditional branches and loops around more than 1 MB of code: b.cond and
   cbz/cbnz reach +-1 MB, so the compiler must relax them (TODO.md tier 1,
   "Branch range").  The bulk is 100k volatile increments, ~1.6 MB.  The
   functions are optnone: clang -O2 takes minutes on such a body, and inline
   asm padding does not work (clang does not relax branches around it). */
#include <stdio.h>

volatile int v, w;
#define S v++;
#define R10(x) x x x x x x x x x x
#define BIG R10(R10(R10(R10(R10(S)))))
#define BULKY __attribute__((noinline, optnone))

BULKY int skip(int x)
{
	if (x)		/* a taken branch over the bulk */
		return 1;
	BIG
	return 2;
}

BULKY int over(int x)
{
	if (!x) {	/* the bulk inside a conditional */
		BIG
	}
	return v;
}

BULKY int loop(int n)
{
	while (n--) {	/* a backward branch over the bulk */
		BIG
	}
	return v;
}

int main(void)
{
	int a = skip(w + 1), b = skip(w);
	int c = over(1);
	int d = over(0);
	int e = loop(2);

	printf("%d %d %d %d %d\n", a, b, c, d, e);
	return !(a == 1 && b == 2 && c == 100000 && d == 200000 && e == 400000);
}
