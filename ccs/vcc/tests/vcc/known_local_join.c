/* vcc-xfail: arm64: a local assigned a constant (an inlined stub's 0) does not decide a later if (TODO.md tier 3, codegen) */
/* A local assigned a constant in straight-line code reads as that constant
   until the next label (mm/shmem.c: orders = an inlined stub's 0, then
   if (orders > 0) { call to a function no one defines }).  The value is
   forgotten wherever paths join: a branch, a loop, a goto, a constant
   switch's break, a statement expression, an inlined copy, ++. */
#include <stdio.h>

int never_defined_orders(void);
static inline unsigned long allow(void) { return 0; }
static int id(int x) { return x; }
static inline int setk(int *p, int v) { *p = v; return v; }

static int shmem_like(int k)
{
	unsigned long orders = 1;

	if (k > 100)
		return -1;
	orders = allow();
	if (orders > 0)
		return never_defined_orders();
	return (int)orders + k;
}

int main(int argc, char **argv)
{
	int a = 1, b = 2, n = 0, s = 0, t = 0, u;
	(void)argv;

	if (argc > 0)
		a = 5;			/* join: a is 1 or 5 */
	for (int i = 0; i < 3; i++)
		b = b + i;		/* loop: b changes */
	n = 3;
again:
	s += n;
	n = n - 1;
	if (n > 0)
		goto again;		/* back edge: n is not 3 at the label */
	switch (2) {
	case 1: t = 10; break;
	case 2: t = 20; break;
	}
	u = ({ int q = 7; if (argc > 5) q = 8; q; });
	t = t + id(1);
	n = 9;
	n++;
	{
		int m = 4;
		int k = setk(&m, 6);	/* (m's address: not tracked) */
		printf("%d %d %d %d %d %d %d %d %d\n", a, b, s, t, u, n, m, k, shmem_like(argc));
	}
	return 0;
}
