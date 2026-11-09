/* Pointers to VLAs step by the run-time element size (TODO.md tier 1,
   "VLA pointer ++/--/+=/-=/difference"); arrays of VLAs (int a[6][n])
   are VLAs themselves. */
#include <stdio.h>

/* a parameter's VLA type keeps the size it had on entry */
__attribute__((noinline)) static long step(int n, int (*a)[n])
{
	int (*a0)[n] = a;

	n += 5;
	a++;
	return (long)sizeof *a * 100 + ((char *)a - (char *)a0);
}

__attribute__((noinline)) int run(int n, int m)
{
	int a[6][n];
	int (*q)[n] = a, (*p)[n] = a;
	int b[4][m][n];
	int (*r)[m][n] = b;
	int bad = 0;

	for (int i = 0; i < 6; i++)
		for (int j = 0; j < n; j++)
			a[i][j] = i * 100 + j;
	q++;		bad |= (*q)[0] != 100;
	q += 2;		bad |= (*q)[1] != 301;
	q--;		bad |= (*q)[0] != 200;
	q -= 1;		bad |= (*q)[0] != 100;
	++q; --q;	bad |= q != a + 1;
	bad |= (a + 5) - p != 5;
	bad |= &a[4] - q != 3;
	r++;		bad |= (char *)r - (char *)b != (long)(m * n * sizeof(int));
	r += 2;		bad |= r - b != 3;
	bad |= sizeof a != 6 * n * sizeof(int) || sizeof b != 4 * m * n * sizeof(int);
	bad |= a[5][n - 1] != 500 + n - 1;
	{
		/* the size is fixed where the declaration is reached */
		int k = n;
		int (*s)[k] = a;

		k += 10;
		s++;		bad |= s != a + 1 || sizeof *s != n * sizeof(int);
	}
	{
		int k = n, v[k];

		k = 1;
		bad |= sizeof v != n * sizeof(int);
		bad |= step(n, a) != (long)n * 4 * 100 + n * 4;
	}
	printf("%d %ld %ld\n", (*q)[0], (long)((a + 5) - p), (long)(r - b));
	return bad;
}

int main(void)
{
	return run(3, 2) | run(7, 5);
}
