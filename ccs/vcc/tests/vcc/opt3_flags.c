/* vcc-flags: -O3 */
/*
 * -O3 flag selection (cond.c, expr.c): ?: with cheap arms as csel, range
 * checks as one unsigned compare (in and out of range, every spelling,
 * signed/unsigned, 32/64-bit, bounds at the type's ends), && / || chains
 * as cmp + ccmp, constant leaves that fold (so guarded code disappears),
 * values and branches.
 */
#include <stdio.h>
#include <limits.h>

static unsigned long h;
__attribute__((noinline)) static void mix(long v) { h = h * 31 + (unsigned long)v; }

#define N(a) (sizeof(a) / sizeof((a)[0]))
static const long vals[] = { 0, 1, -1, 9, 10, 11, 47, 48, 57, 58, 64, 65, 90, 91, 97, 122, 123, 255, 4096, 4097, 100000,
			     -100000, INT_MAX, INT_MIN, 0x80000000L, LONG_MAX, LONG_MIN, 1L << 40 };

/* never defined: a reference left behind would not link */
extern void missing(void);

__attribute__((noinline)) static void ranges(long l)
{
	int i = (int)l;
	unsigned u = (unsigned)l;
	unsigned long ul = (unsigned long)l;
	const int k = 5;

	mix(i >= '0' && i <= '9');
	mix(i < '0' || i > '9');
	mix('a' <= i && i <= 'z');
	mix(i > 64 && i < 91);
	mix(i >= 10 && 100000 >= i);
	mix(i >= -100000 && i <= 4096);
	mix(u >= 10 && u <= 4097);
	mix(u < 48 || u > 57);
	mix(l >= 0 && l <= 0x7fffffff);
	mix(l >= -1 && l <= (1L << 40));
	mix(ul >= 1 && ul <= 0xffffffffUL);
	mix(i >= INT_MIN && i <= 0);
	mix(i >= 0 && i <= INT_MAX);
	mix(u >= 0 && u <= 9);
	if (i >= '0' && i <= '9')
		mix(100);
	if (i < 'A' || i > 'Z')
		mix(101);
	if (k > 3 && k < 9)
		mix(102);
	if (k >= 6 && k <= 9)
		missing();
}

__attribute__((noinline)) static void chains(long l, long m)
{
	int a = (int)l, b = (int)m;
	unsigned ua = (unsigned)l;
	long p = l, q = m;
	char *s = (char *)0 + (l & 0xff), *t = (char *)0 + (m & 0xff);
	const int one = 1, zero = 0;

	mix(a == 1 && b == 2);
	mix(a == 0 || b == 0);
	mix(a < b && b < 100 && a != -1);
	mix(a > b || b > 100 || a == 47);
	mix(!(a < b) && b != 0);
	mix(a == 0 && !(b > 5));
	mix(p < q && q != 0);
	mix(p > 0x1000 || q < -0x1000);
	mix(ua > 5 && b == -3);
	mix(a == 5 && b == -20);
	mix(a == 31 || b == -31 || a == 32 || b == -32);
	mix(s < t && t != (char *)0);
	mix(a > 0 && one);
	mix(zero || a < 3);
	mix(a > 0 && zero);
	mix(one || a < 3);
	if (a < b && b < 64)
		mix(200);
	if (a == 9 || b == 9 || a == b)
		mix(201);
	if (!(a > 3 && b > 3))
		mix(202);
	if (a > 0 && zero)
		missing();
	if (zero && a > 0)
		missing();
}

__attribute__((noinline)) static void selects(long l, long m)
{
	int a = (int)l, b = (int)m;
	unsigned ua = (unsigned)l;
	long p = l, q = m;
	short s = (short)l;
	unsigned char uc = (unsigned char)m;
	const int k = 7;

	mix(a < b ? a : b);
	mix(a > b ? a : b);
	mix(a ? 3 : -4);
	mix(p ? q : 99);
	mix(ua < 10 ? (long)ua : -1);
	mix(s ? (int)s : 1000);
	mix(uc ? uc : 7);
	mix(p < 0 ? -p : p);
	mix(a == b ? 0 : (a < b ? -1 : 1));
	mix(k > 5 ? a : b);
	mix((p & 1) ? p : q);
	mix(a >= 0 && a <= 9 ? a : -1);
}

int main(void)
{
	for (unsigned i = 0; i < N(vals); i++) {
		ranges(vals[i]);
		for (unsigned j = 0; j < N(vals); j += 3) {
			chains(vals[i], vals[j]);
			selects(vals[i], vals[j]);
		}
	}
	printf("%lx\n", h);
	return 0;
}
