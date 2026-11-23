/* A zero constant between a compare and the instruction that reads its flags: the
 * zeroing must not clobber them (x86-64 turns `mov $0, r` into `xor r, r` only where the
 * flags are dead), and a compare with zero keeps its signed, unsigned and equal results. */
#include <stdio.h>

static __attribute__((noinline)) int pick(int a, int b, int x)
{
	int r = 0;
	if (a < b)
		r = x;
	return r;
}

static __attribute__((noinline)) long select_zero(long a, long b, long x, long y)
{
	long r = a > b ? x : 0;
	long s = a == b ? 0 : y;
	return r + s;
}

static __attribute__((noinline)) int compare_zero(int v, unsigned u, long w)
{
	return (v < 0) | (v == 0) << 1 | (u > 0) << 2 | (u == 0) << 3 | (w < 0) << 4 | (w >= 0) << 5;
}

int main(void)
{
	int fail = 0;

	for (int a = -2; a <= 2; a++)
		for (int b = -2; b <= 2; b++) {
			int e = a < b ? 7 : 0;
			long f = (a > b ? 11L : 0) + (a == b ? 0 : 13L);

			if (pick(a, b, 7) != e || select_zero(a, b, 11, 13) != f)
				fail++;
		}
	for (int v = -1; v <= 1; v++)
		for (unsigned u = 0; u <= 1; u++)
			for (long w = -1; w <= 1; w++) {
				int e = (v < 0) | (v == 0) << 1 | (u > 0) << 2 | (u == 0) << 3 | (w < 0) << 4 | (w >= 0) << 5;

				if (compare_zero(v, u, w) != e)
					fail++;
			}
	printf("%d\n", fail);
	return fail != 0;
}
