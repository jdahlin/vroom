/* asm flag outputs "=@cc<cond>": the kernel's CC_SET/CC_OUT (bt in test_bit,
   boot_cpu_has); vcc read %cl for "=@ccc" before. */
#include <stdio.h>

#if defined(__x86_64__)
static int bt(unsigned long long w, long long n) { _Bool c; asm("btq %2,%1" : "=@ccc"(c) : "m"(w), "Ir"(n)); return c; }
static int eqz(int x) { int z; asm("testl %1,%1" : "=@ccz"(z) : "r"(x)); return z; }
static long long above(unsigned a, unsigned b) { long long r; asm("cmpl %2,%1" : "=@cca"(r) : "r"(a), "r"(b)); return r; }
#else
static int bt(unsigned long long w, long long n) { return w >> n & 1; }
static int eqz(int x) { return !x; }
static long long above(unsigned a, unsigned b) { return a > b; }
#endif

int main(void)
{
	unsigned long long caps = 5;
	int n = 0;

	for (long long i = 0; i < 4; i++)
		n += bt(caps, i) << i;
	printf("%d %d %d %d %lld %lld\n", n, bt(caps, 2), eqz(0), eqz(3), above(3, 2), above(2, 3));
	return 0;
}
