/*
 * Calls with more than 128 arguments (the emitter's fixed array): psql's
 * generated sql_help.c passes ~260 to one appendPQExpBuffer.  Also
 * __builtin_bzero (FD_ZERO on macOS).
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long vsum(int n, ...)
{
	va_list ap;
	long s = 0;

	va_start(ap, n);
	for (int k = 0; k < n; k++)
		s += (long)va_arg(ap, int) * (k + 1);
	va_end(ap);
	return s;
}

#define A10(b) b + 0, b + 1, b + 2, b + 3, b + 4, b + 5, b + 6, b + 7, b + 8, b + 9
#define A100(b) A10(b), A10(b + 10), A10(b + 20), A10(b + 30), A10(b + 40), A10(b + 50), A10(b + 60), \
	A10(b + 70), A10(b + 80), A10(b + 90)

/* fixed parameters past 128, mixing int and double */
#define P10(t, x) t x##0, t x##1, t x##2, t x##3, t x##4, t x##5, t x##6, t x##7, t x##8, t x##9
__attribute__((noinline)) static double fixed(P10(int, a), P10(double, b), P10(int, c), P10(int, d), P10(double, e),
					     P10(int, f), P10(int, g), P10(double, h), P10(int, i), P10(int, j),
					     P10(int, k), P10(double, l), P10(int, m), P10(int, n), P10(int, o))
{
	return a0 + b1 + c2 + d3 + e4 + f5 + g6 + h7 + i8 + j9 + k0 + l1 + m2 + n3 + o9;
}

int main(void)
{
	long s = vsum(259, A100(1), A100(101), A10(201), A10(211), A10(221), A10(231), A10(241), 251, 252, 253,
		      254, 255, 256, 257, 258, 259);
	long want = 0;
	double f = fixed(A100(0), A10(100), A10(110), A10(120), A10(130), A10(140));
	char buf[37];

	for (long k = 1; k <= 259; k++)
		want += k * k;
	memset(buf, 'x', sizeof buf);
	__builtin_bzero(buf + 1, 35);
	printf("%ld %.1f %d %d %d\n", s, f, buf[0], buf[1], buf[36]);
	if (s != want || f != 0 + 11 + 22 + 33 + 44 + 55 + 66 + 77 + 88 + 99 + 100 + 111 + 122 + 133 + 149 ||
	    buf[0] != 'x' || buf[1] || buf[35] || buf[36] != 'x')
		abort();
	return 0;
}
