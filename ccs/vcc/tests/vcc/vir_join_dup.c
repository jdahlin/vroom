/* VIR lowering: a join that only tests what its predecessors computed is copied to the end
 * of each predecessor that would jump to it.  Three-way compare results through a join,
 * a join whose value comes from calls, constants and loads, a join inside a loop, chained
 * joins, and a join whose test reads a value changed on one arm only. */
#include <stdio.h>
#define NI __attribute__((noinline))

NI static int cmp1(long a, long b) { return a < b ? -1 : a > b; }
NI static int cmp2(long a, long b) { return a % 7 < b % 7 ? -1 : a % 7 > b % 7; }

NI static long search(const long *p, long n, long key, int how)
{
	long lo = 0, hi = n - 1, steps = 0;

	while (lo <= hi) {
		long mid = (lo + hi) >> 1;
		int c;

		if (how == 0)
			c = cmp1(p[mid], key);
		else if (how == 1)
			c = cmp2(p[mid], key) ? cmp1(p[mid], key) : 0;
		else
			c = p[mid] < key ? -1 : p[mid] != key;
		steps++;
		if (c < 0)
			lo = mid + 1;
		else if (c > 0)
			hi = mid - 1;
		else
			return mid * 100 + steps;
	}
	return -lo * 100 - steps;
}

NI static long chain(long a, long b, long c)
{
	long x, y;

	if (a > 0)
		x = a + b;
	else if (b > 0)
		x = b - c;
	else
		x = c;
	if (x & 1)
		y = x * 3;
	else
		y = x >> 1;
	if (y > 10)
		return y - a;
	if (y < -10)
		return y + b;
	return y ^ c;
}

NI static long onearm(long a, long b)
{
	long t = a;

	if (b & 1)
		t = a + b;
	if (t > 5)
		return t * 2;
	return t - 1;
}

int main(void)
{
	long p[16];

	for (int i = 0; i < 16; i++)
		p[i] = i * 3 + 1;
	for (int how = 0; how < 3; how++) {
		for (long k = -1; k < 50; k += 3)
			printf("%ld ", search(p, 16, k, how));
		printf("%ld %ld\n", search(p, 0, 4, how), search(p, 1, 1, how));
	}
	for (long a = -2; a <= 2; a++)
		for (long b = -9; b <= 9; b += 6)
			printf("%ld %ld %ld ", chain(a, b, a * b), chain(b, a, 11), onearm(a * 3, b));
	printf("\n");
	return 0;
}
