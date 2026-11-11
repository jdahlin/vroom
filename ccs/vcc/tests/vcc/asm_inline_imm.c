/* An inlined parameter bound to a constant argument is an asm "i" operand,
   even when the same asm writes it (the kernel's rip_rel_ptr:
   asm("leaq %c1(%%rip), %0" : "=r"(p) : "i"(p))). */
#include <stdio.h>

int tbl[4] = { 1, 2, 3, 4 };

static inline __attribute__((always_inline)) void *rel(void *p)
{
#ifdef __x86_64__
	asm("leaq %c1(%%rip), %0" : "=r"(p) : "i"(p));
#endif
	return p;
}

static inline __attribute__((always_inline)) int twice(int k)
{
	int r = k;
#ifdef __x86_64__
	asm("lea %c1(%1), %0" : "=r"(r) : "i"(k), "0"(r));
#else
	r = k + k;
#endif
	return r;
}

int main(void)
{
	int *p = rel(tbl);

	printf("%d %d %d\n", p[2], p == tbl, twice(21));
	return 0;
}
