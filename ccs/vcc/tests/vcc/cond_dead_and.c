/* a && b where a is known false (__builtin_constant_p of a parameter in an
   out-of-line copy): b is dead and not generated, as clang -O2 (the kernel's
   nf_hook: an asm "i" operand in b that is only constant when inlined). */
#include <stdio.h>

extern int never_defined(int);

int __attribute__((noinline)) hook(int pf)
{
	if (__builtin_constant_p(pf) && never_defined(pf))
		return -1;
	if (!__builtin_constant_p(pf) || never_defined(pf + 1))
		return pf + 1;
	return 0;
}

int main(void)
{
	volatile int v = 41;

	printf("%d\n", hook(v));
	return 0;
}
