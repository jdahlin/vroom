/* __builtin_flt_rounds (FLT_ROUNDS) reads the current rounding mode, it is
 * not the constant 1: each FPCR.RMode gives C's value (CPython's floatobject.c).
 * arm64 only: the x86-64 and wasm lowerings are not checked here. */
#include <float.h>
#include <stdio.h>

#if defined(__aarch64__)
static unsigned long get_fpcr(void) { unsigned long v; __asm__ volatile("mrs %0, fpcr" : "=r"(v)); return v; }
static void set_fpcr(unsigned long v) { __asm__ volatile("msr fpcr, %0" : : "r"(v)); }

int main(void)
{
	unsigned long saved = get_fpcr();
	for (unsigned long rm = 0; rm < 4; rm++) {
		set_fpcr((saved & ~(3ul << 22)) | rm << 22);
		int r = FLT_ROUNDS;
		set_fpcr(saved);
		printf("rmode %lu: %d\n", rm, r);	/* 1 0 2 3 */
	}
	printf("default: %d\n", __builtin_flt_rounds());
	return 0;
}
#else
int main(void) { return 0; }
#endif
