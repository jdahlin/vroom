/* VIR allocator: values that reach a loop in their spill slots (they crossed calls with
 * the callee-saved registers taken) and are read in the loop get registers on the edge
 * into it; a call's arguments that shift between argument registers are a parallel move. */
#include <stdio.h>

__attribute__((noinline)) static long g(long x) { return x * 3 + 1; }

__attribute__((noinline)) static long shift(long a, long b, long c, long d, long e, long f)
{
	return a + 2 * b + 3 * c + 5 * d + 7 * e + 11 * f;
}

/* 14 values live across calls, then a loop without calls that reads four of them */
__attribute__((noinline)) static long loop(long *p, long n, long a, long b, long c, long d, long e, long h)
{
	long q = g(a), r = g(b), s = g(c), t = g(d), u = g(e), v = g(h), w = g(n);
	long acc = 0, k = n;

	for (long i = 0; i < n; i++) {
		acc += (p[i] & q) + r;
		p[i] = acc ^ s;
		k += t;
	}
	return g(acc + k + a + b + c + d + e + h + u + v + w);
}

/* the same with a call in the loop: only a callee-saved register keeps a value */
__attribute__((noinline)) static long loopcall(long *p, long n, long a, long b, long c, long d, long e, long h)
{
	long q = g(a), r = g(b), s = g(c), t = g(d), u = g(e), v = g(h), w = g(n);
	long acc = 0, k = n;

	for (long i = 0; i < n; i++) {
		acc += g(p[i] & q) + r;
		p[i] = acc ^ s;
		k += t;
	}
	return acc + k + a + b + c + d + e + h + u + v + w;
}

/* arguments rotate through the argument registers, one is passed twice, one is a constant */
__attribute__((noinline)) static long args(long a, long b, long c, long d)
{
	long x = shift(96, a, b, 0, c, b);
	long y = shift(b, c, d, a, a, 5);
	long z = shift(d, a, b, c, d, a);

	return x ^ y ^ z ^ shift(b, a, d, c, b, a);
}

int main(void)
{
	long p[9] = { 3, 1, 4, 1, 5, 9, 2, 6, 5 }, q[9] = { 2, 7, 1, 8, 2, 8, 1, 8, 2 };

	printf("%ld\n", loop(p, 9, 11, 12, 13, 14, 15, 16));
	printf("%ld\n", loopcall(q, 9, 21, 22, 23, 24, 25, 26));
	for (int i = 0; i < 9; i++)
		printf("%ld %ld\n", p[i], q[i]);
	printf("%ld\n", args(1, 20, 300, 4000));
	return 0;
}
