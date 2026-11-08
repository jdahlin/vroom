/* Pointers to VLAs step by the run-time element size (TODO.md tier 1,
   "VLA pointer ++/--/+=/-=/difference"). */
/* vcc-xfail: VLA pointer ++/--/+=/-=/difference ignore runtime size */
#include <stdio.h>

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
	printf("%d %ld %ld\n", (*q)[0], (long)((a + 5) - p), (long)(r - b));
	return bad;
}

int main(void)
{
	return run(3, 2) | run(7, 5);
}
