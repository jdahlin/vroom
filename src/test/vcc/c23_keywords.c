/* vcc-flags: -std=c23 */
/* C23 keywords (bool true false nullptr alignas alignof static_assert
   thread_local typeof_unqual constexpr), one-argument static_assert, digit
   separators, #if true (TODO.md tier 3, C23) */
#include <stdio.h>

static_assert(sizeof(int) == 4);
static_assert(alignof(long) == 8, "alignof");
extern thread_local int tl;	/* (declared only: ELF TLS is a TODO.md item of its own) */

#if true
#define TRUE_IN_IF 1
#else
#define TRUE_IN_IF 0
#endif

int main(void)
{
	bool b = true, f = false;
	int *p = nullptr;
	typeof_unqual(const int) x = 3;
	constexpr int k = 5;
	alignas(16) int al = 1;
	long big = 1'000'000, hex = 0xFF'FF;
	double d = 1'0.2'5;
	int z[2] = {};

	x++;	/* typeof_unqual dropped the const */
	printf("%d %d %d %d %d %d %ld %ld %g %d %d %d\n", b, f, p == 0, x, k, al, big, hex, d, z[1],
	       TRUE_IN_IF, (int)sizeof(tl));
	printf("%zu %d\n", sizeof(b), (int)(((unsigned long)&al) % 16));
	return !true;
}
