/* inline asm %=: a number unique per asm instance in the object (inlined
   copies included) and independent of the emitter's threads (TODO.md tier 1,
   "Inline asm %= depends on thread scheduling").  Clang rejects %= on
   AArch64, so the test checks only what GCC promises: the labels do not
   clash and each instance branches to its own. */
/* vcc-noref: clang rejects %= on AArch64 */
/* vcc-threads-same */
#include <stdlib.h>

static inline __attribute__((always_inline)) int pick(int v)
{
	int r;

	__asm__ volatile("cbz %w1, .Lz%=\n\tmov %w0, #1\n\tb .Le%=\n.Lz%=:\n\tmov %w0, #2\n.Le%=:"
			 : "=r"(r) : "r"(v));
	return r;
}

int f1(int a) { return pick(a) * 10 + pick(!a); }
int f2(int a) { return pick(a) + pick(a) + pick(!a); }
#define G(n) int g##n(int a) { return pick(a + n) + pick(a); }
#define G10(n) G(n##0) G(n##1) G(n##2) G(n##3) G(n##4) G(n##5) G(n##6) G(n##7) G(n##8) G(n##9)
G10(1) G10(2) G10(3) G10(4)

int main(void)
{
	if (f1(0) != 21 || f1(1) != 12)
		abort();
	if (f2(0) != 5 || f2(1) != 4)
		abort();
	if (g10(0) != 3 || g49(1) != 2)
		abort();
	return 0;
}
