/* VIR lowering: a while/for loop's test is laid out after the loop's last block and the
 * edges into the loop get a copy of it.  Zero, one and many rounds; nested loops; continue
 * and break; a loop entered from both arms of a branch; a test with a side effect on the
 * values it compares; a loop reached by goto past its test; values live round the loop. */
#include <stdio.h>
#define NI __attribute__((noinline))

NI static long sum(const long *p, long n) { long s = 0; for (long i = 0; i < n; i++) s += p[i]; return s; }

NI static long nest(long (*p)[4], long n, long m)
{
	long s = 0;

	for (long i = 0; i < n; i++)
		for (long j = 0; j < m; j++)
			s = s * 3 + p[i][j];
	return s;
}

NI static long skips(const long *p, long n)
{
	long s = 0, i = 0;

	while (i < n) {
		long v = p[i++];

		if (v & 1)
			continue;
		if (v > 90)
			break;
		s += v;
	}
	return s * 100 + i;
}

NI static long twoways(const long *p, long n, int f)
{
	long i, s;

	if (f) {
		i = 1;
		s = 1000;
	} else {
		i = 0;
		s = -p[0];
	}
	while (i < n && p[i] != 7)
		s += p[i++];
	return s + i;
}

NI static long str(const char *z)
{
	long n = 0, h = 0;

	while (*z) {
		h = h * 31 + *z++;
		n++;
	}
	return h ^ n;
}

NI static long middle(const long *p, long n, int skip)
{
	long i = 0, s = 0;

	if (skip)
		goto body;
	while (i < n) {
body:
		s += p[i] + (i < n);
		i++;
	}
	return s;
}

NI static long counted(long n, long a, long b, long c, long d)
{
	long x = a, i;

	for (i = n; i > 0; i -= 2)
		x = x * b + c - d + i;
	return x + i + a + b + c + d;
}

int main(void)
{
	long p[10] = { 3, 14, 15, 92, 6, 5, 35, 8, 97, 7 };
	long q[3][4] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 } };

	for (long n = 0; n <= 10; n++)
		printf("%ld %ld %ld %ld %ld %ld\n", sum(p, n), skips(p, n), twoways(p, n, 0), twoways(p, n, 1),
		       middle(p, n, 0), counted(n, 2, 3, 5, 7));
	for (long n = 0; n <= 3; n++)
		for (long m = 0; m <= 4; m++)
			printf("%ld ", nest(q, n, m));
	printf("\n%ld %ld %ld\n", middle(p, 0, 1), middle(p, 4, 1), str(""));
	printf("%ld %ld\n", str("a"), str("loop rotation"));
	return 0;
}
