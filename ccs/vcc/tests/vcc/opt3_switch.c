/* vcc-flags: -O3 */
/*
 * -O3 switch lowering (stmt.c switch_tree): dense runs as branch tables
 * (with and without a bias, 32- and 64-bit, signed and unsigned, case
 * ranges, no default), short runs as compare chains, sparse sets as a
 * binary search, nested switches, fall-through, break/continue in loops,
 * and a table in a function with inline asm (the object writer's other
 * path).
 */
#include <stdio.h>

static unsigned long h;
__attribute__((noinline)) static void mix(long v) { h = h * 31 + (unsigned long)v; }

__attribute__((noinline)) static int dense(int v)
{
	switch (v) {
	case 0: return 10;
	case 1: return 11;
	case 2: return 12;
	case 3: return 13;
	case 5: return 15;
	case 6: return 16;
	case 8: return 18;
	default: return -1;
	}
}

__attribute__((noinline)) static int biased(int v)
{
	int r = 0;

	switch (v) {
	case -3: r = 1; break;
	case -2: r = 2; /* fall through */
	case -1: r += 3; break;
	case 0: r = 4; break;
	case 1 ... 4: r = 5; break;
	case 7: r = 6; break;
	}
	return r;
}

__attribute__((noinline)) static long wide(long v)
{
	switch (v) {
	case 0x100000000L: return 1;
	case 0x100000001L: return 2;
	case 0x100000002L: return 3;
	case 0x100000003L: return 4;
	case 0x100000005L: return 5;
	default: return 0;
	}
}

__attribute__((noinline)) static int uns(unsigned v)
{
	switch (v) {
	case 0xfffffff0u: return 1;
	case 0xfffffff1u: return 2;
	case 0xfffffff2u: return 3;
	case 0xfffffff3u: return 4;
	case 0xffffffffu: return 5;
	case 3: return 6;
	case 4: return 7;
	default: return 8;
	}
}

__attribute__((noinline)) static int chr(signed char c)
{
	switch (c) {
	case 'a' ... 'f': return c - 'a' + 10;
	case '0' ... '9': return c - '0';
	case 'A' ... 'F': return c - 'A' + 10;
	case -128: return 100;
	case 127: return 101;
	}
	return -1;
}

__attribute__((noinline)) static int sparse(long v)
{
	switch (v) {
	case -1000000: return 1;
	case -7: return 2;
	case 3: return 3;
	case 100: return 4;
	case 1000: return 5;
	case 4096: return 6;
	case 65536: return 7;
	case 1L << 40: return 8;
	case 1000000: return 9;
	case 12345678: return 10;
	case 0x7fffffffffffffffL: return 11;
	case -0x7fffffffffffffffL - 1: return 12;
	default: return 0;
	}
}

__attribute__((noinline)) static int mixed(int v)
{
	switch (v) {
	case 10: case 11: case 12: case 13: case 14: case 15: return 1;
	case 500: return 2;
	case 9000: return 3;
	case 9001: return 4;
	case 20000: case 20001: case 20002: case 20003: case 20004: return 5;
	case 30000 ... 30900: return 6;
	case -50: return 7;
	default: return 8;
	}
}

__attribute__((noinline)) static long nested(int a, int b)
{
	long s = 0;

	for (int k = 0; k < 6; k++) {
		switch ((a + k) & 7) {
		case 0:
			switch (b) {
			case 0: s += 1; break;
			case 1: s += 2; break;
			case 2: s += 3; break;
			case 3: s += 4; break;
			default: s += 5; continue;
			}
			s *= 2;
			break;
		case 1: s += 10; break;
		case 2: s -= 3; /* fall through */
		case 3: s ^= 0x55; break;
		case 4: continue;
		case 5: s += b; break;
		case 6: if (b > 2) goto out; break;
		default: s += 1000;
		}
		s += k;
	}
out:
	return s;
}

__attribute__((noinline)) static int withasm(int v)
{
	int r;

	__asm__ volatile("" ::: "memory");
	switch (v) {
	case 1: r = 7; break;
	case 2: r = 8; break;
	case 3: r = 9; break;
	case 4: r = 10; break;
	case 6: r = 11; break;
	default: r = 0;
	}
	__asm__ volatile("" ::: "memory");
	return r;
}

static const long vals[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -2, -3, -4, 10, 15, 16, 48, 57, 65, 70, 97, 102, 103,
			     127, -128, 100, 500, 1000, 4096, 9000, 9001, 20002, 30000, 30500, 30900, 30901, -50, -7,
			     -1000000, 65536, 1000000, 12345678, 1L << 40, 0x100000000L, 0x100000003L, 0x100000004L,
			     0x100000005L, 0xfffffff0L, 0xfffffff3L, 0xfffffff4L, 0xffffffffL, 0x7fffffffffffffffL,
			     -0x7fffffffffffffffL - 1 };

int main(void)
{
	for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
		long v = vals[i];

		mix(dense((int)v)), mix(biased((int)v)), mix(wide(v)), mix(uns((unsigned)v)), mix(chr((signed char)v));
		mix(sparse(v)), mix(mixed((int)v)), mix(withasm((int)v));
		for (int b = 0; b < 5; b++)
			mix(nested((int)v, b));
	}
	printf("%lx\n", h);
	return 0;
}
