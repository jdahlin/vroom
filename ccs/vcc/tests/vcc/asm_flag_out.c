/* asm flag outputs "=@cc<cond>": the kernel's CC_SET/CC_OUT (bt in test_bit,
   boot_cpu_has); vcc read %cl for "=@ccc" before. */
#include <stdio.h>

#if defined(__x86_64__)
static int bt(unsigned long w, long n) { _Bool c; asm("btq %2,%1" : "=@ccc"(c) : "m"(w), "Ir"(n)); return c; }
static int eqz(int x) { int z; asm("testl %1,%1" : "=@ccz"(z) : "r"(x)); return z; }
static long above(unsigned a, unsigned b) { long r; asm("cmpl %2,%1" : "=@cca"(r) : "r"(a), "r"(b)); return r; }
#elif defined(__aarch64__)
static int bt(unsigned long w, long n) { int c; asm("tst %1, %2\n" : "=@ccne"(c) : "r"(w), "r"(1ul << n)); return c; }
static int eqz(int x) { int z; asm("cmp %w1, #0" : "=@cceq"(z) : "r"(x)); return z; }
static long above(unsigned a, unsigned b) { long r; asm("cmp %w1, %w2" : "=@cchi"(r) : "r"(a), "r"(b)); return r; }
#else
static int bt(unsigned long w, long n) { return w >> n & 1; }
static int eqz(int x) { return !x; }
static long above(unsigned a, unsigned b) { return a > b; }
#endif

int main(void)
{
	unsigned long caps = 5;
	int n = 0;

	for (long i = 0; i < 4; i++)
		n += bt(caps, i) << i;
	printf("%d %d %d %d %ld %ld\n", n, bt(caps, 2), eqz(0), eqz(3), above(3, 2), above(2, 3));
	return 0;
}
