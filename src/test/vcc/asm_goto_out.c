/* asm goto with register outputs (the kernel's CC_HAS_ASM_GOTO_OUTPUT): the
   outputs hold on the fall-through and on every label edge (x86-64: a
   trampoline per label stores them). */
#include <stdio.h>

#ifdef __x86_64__
static int f(int x)
{
	int y;
	asm goto("movl %1, %0\n\taddl $1, %0\n\tcmpl $10, %0\n\tjg %l[big]" : "=&r"(y) : "r"(x) : "cc" : big);
	return y;
big:
	return -y;
}
static long long g(long long *p)
{
	asm goto("addq $5, %0\n\ttestq $1, %0\n\tjnz %l[odd]" : "+r"(*p) : "r"(0LL) : "cc" : odd);
	return 0;
odd:
	return 1;
}
#else
static int f(int x) { int y = x + 1; return y > 10 ? -y : y; }
static long long g(long long *p) { *p += 5; return *p & 1; }
#endif

int main(void)
{
	long long v = 2, w = 3;
	int a = f(3), b = f(20);
	long long c = g(&v), d = g(&w);

	printf("%d %d %lld %lld %lld %lld\n", a, b, c, v, d, w);
	return 0;
}
