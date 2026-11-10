/* vcc-flags: -O3 */
/*
 * -O3 branch selection (label.c bcond_opt, arith.c test_zero, cond.c):
 * compares with zero as cbz/cbnz, signs and single bits as tbz/tbnz,
 * bools tested on bit 0, conditions through widening casts and
 * __builtin_expect, extensions and bool conversions the last record makes
 * redundant, and a function too large for tbz's +-32 KiB (tbz_range).
 */
#include <stdio.h>
#include <stdbool.h>

static unsigned long h;
__attribute__((noinline)) static void mix(long v) { h = h * 31 + (unsigned long)v; }

#define N(a) (sizeof(a) / sizeof((a)[0]))
static const long vals[] = { 0, 1, -1, 2, 0x40, 0x41, 0x80, 0xff, 0x100, 0x7fffffff, -0x80000000L, 0x80000000L,
			     1L << 40, (1L << 40) + 1, -(1L << 62), 0x7fffffffffffffffL };

__attribute__((noinline)) static void conds(long l)
{
	int i = (int)l;
	unsigned u = (unsigned)l;
	unsigned long ul = (unsigned long)l;
	signed char sc = (signed char)l;
	unsigned char uc = (unsigned char)l;
	short s = (short)l;
	bool b = l & 4;

	if (i == 0) mix(1);
	if (i != 0) mix(2);
	if (u > 0) mix(3);
	if (u <= 0) mix(4);
	if (i < 0) mix(5);
	if (i >= 0) mix(6);
	if (l < 0) mix(7);
	if (l >= 0) mix(8);
	if (ul > 0) mix(9);
	if (i & 0x40) mix(10);
	if (!(i & 0x80)) mix(11);
	if (l & (1L << 40)) mix(12);
	if ((l & (1L << 63)) == 0) mix(13);
	if (u & 0x80000000u) mix(14);
	if (sc) mix(15);
	if (!uc) mix(16);
	if (s) mix(17);
	if (b) mix(18);
	if (!b) mix(19);
	if ((long)i) mix(20);
	if ((unsigned long)u) mix(21);
	if (__builtin_expect(i > 5, 0)) mix(22);
	if (__builtin_expect(!!(l & 2), 1)) mix(23);
	if ((char)i == 0) mix(24);
	mix((bool)(i < 3));
	mix((bool)!(l > 7));
	mix((bool)(i && l > 2));
	mix(!(i < 9));
	mix((long)sc + (long)uc + (long)s);
	mix((unsigned long)(unsigned)i);
	mix(!!sc);
	mix(!!(i & 0x100));
	while (i > 0 && (i & 1) == 0)
		i >>= 1;
	mix(i);
}

/* many blocks: the function exceeds tbz's reach, its bit tests go back to tst + b.cond */
#define T1(k) if (x & (1u << ((k) % 31))) acc += (k); else acc ^= (k) * 3; if (acc < 0) acc = -acc;
#define T10(k) T1(k) T1(k + 1) T1(k + 2) T1(k + 3) T1(k + 4) T1(k + 5) T1(k + 6) T1(k + 7) T1(k + 8) T1(k + 9)
#define T100(k) T10(k) T10(k + 10) T10(k + 20) T10(k + 30) T10(k + 40) T10(k + 50) T10(k + 60) T10(k + 70) T10(k + 80) T10(k + 90)
__attribute__((noinline)) static long big(unsigned x)
{
	long acc = 0;

	T100(0) T100(100) T100(200) T100(300) T100(400) T100(500) T100(600) T100(700) T100(800) T100(900)
	T100(1000) T100(1100)
	return acc;
}

int main(void)
{
	for (unsigned k = 0; k < N(vals); k++)
		conds(vals[k]);
	mix(big(0x12345678)), mix(big(0xfedcba98)), mix(big(0));
	printf("%lx\n", h);
	return 0;
}
