/* An "i" operand that is not a constant expression but whose every bit is
   known: runtime values ORed in and masked away again, as the kernel's
   PIE_E1 in asm-offsets.c (`arm64_use_ng_mappings ? PTE_NG : 0`,
   `lpa2_is_enabled() ? 0 : ...`).  Clang takes it from LLVM's known bits at
   -O1 and above; vcc failed with "impossible constraint in asm: 'i'" and
   vccd sent the file to clang.  The call inside must still be made. */
#include <stdlib.h>

volatile _Bool flag = 1;
static int calls;

__attribute__((noinline)) _Bool probe(void)
{
	calls++;
	return flag;
}

#define IMM(x) ({ unsigned long long r_; __asm__("ldr %0, =%1" : "=r"(r_) : "i"(x)); r_; })

typedef unsigned long long pteval_t;

int main(void)
{
	/* the PIE_E1 shape: bits 8, 9 and 11 unknown, bit 54 tested */
	if (IMM(((((pteval_t)3 | (flag ? 1ULL << 11 : 0) | (probe() ? 0 : 3ULL << 8) | 1ULL << 54) & (1ULL << 54)) >> 51) * 4) != 32)
		abort();
	if (calls != 1)
		abort();
	/* & with a known 0, | with a known 1, ~, ^ of known halves */
	if (IMM((flag & 0) | 5) != 5)
		abort();
	if (IMM((unsigned)((flag | 0xff) & 0x0f)) != 0x0f)
		abort();
	if (IMM(~((unsigned long long)flag << 8 & 0) ^ 0xf0ULL) != ~0xf0ULL)
		abort();
	/* both arms of ?: agree on the bits kept */
	if (IMM((flag ? 0x13ULL : 0x17ULL) & 0x3) != 3)
		abort();
	/* shifts: unsigned >> fills with known zeros, signed >> with the known sign */
	if (IMM(((unsigned long long)flag << 60 | 0xf000000000000000ULL) >> 60) != 0xf)
		abort();
	if (IMM((long long)(((unsigned long long)flag | 0x8000000000000000ULL) >> 1 & 0xc000000000000000ULL)) != 0x4000000000000000ULL)
		abort();
	if (IMM(((long long)((unsigned long long)flag | 0x8000000000000000ULL) >> 62) & ~1LL) != (unsigned long long)-2LL)
		abort();
	/* narrow types: a signed char's known sign extends, an unsigned one's zeros */
	if (IMM((long long)(signed char)((unsigned char)flag | 0x80) >> 8) != (unsigned long long)-1LL)
		abort();
	if (IMM((long long)(unsigned char)((unsigned)flag << 9 | 0x7f)) != 0x7f)
		abort();
	/* a known nonzero bit makes a _Bool 1 */
	if (IMM((int)(_Bool)((unsigned)flag | 4)) != 1)
		abort();
	return 0;
}
