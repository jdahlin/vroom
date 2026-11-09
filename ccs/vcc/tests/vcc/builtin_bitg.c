/* __builtin_clzg / ctzg / popcountg: clang's type-generic bit counts, with
   and without the zero fallback, on every unsigned width up to 64 (found by
   make self-check: vcc's lexer uses __builtin_clzg). */
#include <stdio.h>

static volatile unsigned char uc = 0x10;
static volatile unsigned short us = 0x100;
static volatile unsigned u = 0x80;
static volatile unsigned long ul = 1ul << 40;
static volatile unsigned long long z = 0;

int main(void)
{
	unsigned char c = uc;
	unsigned short s = us;
	unsigned i = u;
	unsigned long l = ul;
	unsigned long long zz = z;

	printf("clz %d %d %d %d\n", __builtin_clzg(c), __builtin_clzg(s), __builtin_clzg(i), __builtin_clzg(l));
	printf("ctz %d %d %d %d\n", __builtin_ctzg(c), __builtin_ctzg(s), __builtin_ctzg(i), __builtin_ctzg(l));
	printf("pop %d %d %d %d\n", __builtin_popcountg(c), __builtin_popcountg((unsigned char)0xff),
	       __builtin_popcountg(~i), __builtin_popcountg(l | 3));
	printf("fb %d %d %d %d\n", __builtin_clzg(zz, 64), __builtin_ctzg(zz, -1), __builtin_clzg(c, 99),
	       __builtin_clzg((unsigned char)0, 8));
	_Static_assert(__builtin_clzg((unsigned char)1) == 7, "constant");
	_Static_assert(__builtin_popcountg(0xffu) == 8, "constant");
	return 0;
}
