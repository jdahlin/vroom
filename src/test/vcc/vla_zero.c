/* int a[n] = {} zeroes the VLA (C23; a GNU extension before), and only
   the VLA: the bytes around it stay (TODO.md tier 1, "VLA with
   initializer"). */
#include <stdio.h>
#include <string.h>

struct al { _Alignas(64) char c; };

__attribute__((noinline)) static void dirty(int n)
{
	volatile char junk[4096];

	for (int i = 0; i < 4096; i++)
		junk[i] = (char)(i | 1);
	(void)n;
}

__attribute__((noinline)) static int run(int n, int m)
{
	int bad = 0;

	dirty(n);
	{
		char guard_lo[16];
		memset(guard_lo, 0x5a, sizeof guard_lo);
		char s[n] = {};
		int a[m][n] = {};
		struct al v[n] = {};
		long z[0 * n + 1] = {};
		char guard_hi[16];
		memset(guard_hi, 0xa5, sizeof guard_hi);

		for (int i = 0; i < n; i++)
			bad |= s[i] != 0 || v[i].c != 0;
		for (int i = 0; i < m; i++)
			for (int j = 0; j < n; j++)
				bad |= a[i][j] != 0;
		bad |= z[0] != 0;
		bad |= ((unsigned long)v & 63) != 0;
		for (int i = 0; i < 16; i++)
			bad |= guard_lo[i] != 0x5a || (unsigned char)guard_hi[i] != 0xa5;
	}
	return bad;
}

int main(void)
{
	int bad = run(1, 1) | run(3, 5) | run(13, 2) | run(100, 7);

	printf("%d\n", bad);
	return bad;
}
