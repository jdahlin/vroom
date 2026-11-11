/* asm outputs through pointers with every operand in a fixed register (the
   kernel's native_cpuid: =a =b =c =d (*p) and tied inputs): the output
   addresses wait in slots, not in temporaries (x86-64 ran out of them). */
#include <stdio.h>

typedef unsigned int u32;

static inline void four(u32 *a, u32 *b, u32 *c, u32 *d)
{
#ifdef __x86_64__
	asm volatile("lea 1(%%rax), %%eax\n\tmov $2, %%ebx\n\tlea 3(%%rcx), %%ecx\n\tmov $4, %%edx"
		     : "=a" (*a), "=b" (*b), "=c" (*c), "=d" (*d) : "0" (*a), "2" (*c) : "memory");
#else
	*a += 1, *b = 2, *c += 3, *d = 4;
#endif
}

int main(void)
{
	u32 a = 10, b, c = 20, d, v[4] = { 5, 0, 6, 0 };

	four(&a, &b, &c, &d);
	four(&v[0], &v[1], &v[2], &v[3]);
	printf("%u %u %u %u %u %u %u %u\n", a, b, c, d, v[0], v[1], v[2], v[3]);
	return 0;
}
