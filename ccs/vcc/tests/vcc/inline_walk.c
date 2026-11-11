/* Inlined calls (always_inline and small inline functions, as clang -O2):
   the same function in its own argument, returns from loops and switches,
   labels and cases in two copies, record and float results, a cleanup and
   a goto inside the body, __builtin_constant_p of a parameter. */
#include <stdio.h>

static int log_[64], nlog;
static void cl(int *p) { log_[nlog++] = *p; }

static inline __attribute__((always_inline)) int add(int a, int b) { return a + b; }
static inline int pick(int k)
{
	switch (k) {
	case 0: return 10;
	case 1: return 11;
	default: break;
	}
	for (int i = 0;; i++)
		if (i * i > k)
			return i;
}
static inline __attribute__((always_inline)) int lab(int n)
{
	int s = 0;
again:
	s += n--;
	if (n > 0)
		goto again;
	return s;
}
struct pt { int x; double y; };
static inline __attribute__((always_inline)) struct pt mk(int x, double y) { struct pt p = { x * 2, y / 2 }; return p; }
static inline __attribute__((always_inline)) double half(double d) { return d * 0.5; }
static inline __attribute__((always_inline)) int clean(int v)
{
	__attribute__((cleanup(cl))) int a = v;
	{
		__attribute__((cleanup(cl))) int b = v + 1;

		if (v > 2)
			goto out;
		a += b;
	}
out:
	return a;
}
static inline __attribute__((always_inline)) int isk(int x) { return __builtin_constant_p(x); }
static inline __attribute__((always_inline)) int isk2(int x) { return __builtin_constant_p(x + 1) && x == 4; }

int main(void)
{
	volatile int v = 5;
	int r = add(add(v, 1), add(add(2, v), 3));
	struct pt p = mk(v, 3.0);
	__attribute__((cleanup(cl))) int outer = 99;

	printf("%d %d %d %d %d\n", r, pick(0), pick(v), pick(1), pick(50));
	printf("%d %d\n", lab(v), lab(3));
	printf("%d %g %g\n", p.x, p.y, half(v) + half(1.0));
	printf("%d %d", clean(1), clean(v));
	for (int i = 0; i < nlog; i++)
		printf(" %d", log_[i]);
	printf("\n%d %d %d %d\n", isk(3), isk(v), isk2(4), isk2(v));
	return 0;
}
