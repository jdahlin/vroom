/* vcc-flags: -O3 */
/*
 * -O3 arithmetic selection (arith.c, lval.c): division and remainder by
 * powers of two (signed: rounding toward zero, the remainder's sign), x *
 * (2^k + 1) as add lsl, c +- a * b as madd/msub, p + i * size as one madd,
 * compares with immediates at the compare's width.
 */
#include <stdio.h>
#include <limits.h>

#define N(a) (sizeof(a) / sizeof((a)[0]))

static const int iv[] = { 0, 1, -1, 2, -2, 7, -7, 8, -8, 9, -9, 1023, -1023, 1024, -1024, INT_MAX, INT_MIN, INT_MIN + 1 };
static const long lv[] = { 0, 1, -1, 15, -15, 16, -16, 17, -17, (long)1 << 40, -((long)1 << 40) - 3, LONG_MAX, LONG_MIN, LONG_MIN + 1 };

__attribute__((noinline)) static unsigned long hash(unsigned long h, long v) { return h * 31 + (unsigned long)v; }

struct s3 { char c[3]; };
struct s12 { int a, b, c; };
struct s24 { long a, b, c; };

int main(void)
{
	unsigned long h = 0;

	for (unsigned k = 0; k < N(iv); k++) {
		volatile int vx = iv[k];
		int x = vx;
		unsigned u = (unsigned)x;

		h = hash(h, x / 1), h = hash(h, x / 2), h = hash(h, x / 4), h = hash(h, x / 8), h = hash(h, x / 1024);
		h = hash(h, x / 65536), h = hash(h, x / (1 << 30));
		h = hash(h, x % 1), h = hash(h, x % 2), h = hash(h, x % 4), h = hash(h, x % 16), h = hash(h, x % 4096);
		h = hash(h, u / 2), h = hash(h, u / 64), h = hash(h, u % 8), h = hash(h, u % 2048), h = hash(h, u / 0x80000000u);
		h = hash(h, u % 0x80000000u);
		h = hash(h, (int)(u * 3)), h = hash(h, (int)(u * 5)), h = hash(h, (int)(u * 9)), h = hash(h, (int)(u * 17));
		h = hash(h, (int)(u * 257)), h = hash(h, (x & 0xffff) * 3), h = hash(h, (x >> 8) * 9);
		h = hash(h, (int)(u * 65u));
		h = hash(h, x > 4095), h = hash(h, x < -4096), h = hash(h, x == 0x1000), h = hash(h, x != -0x5000);
		h = hash(h, x >= 0x123456), h = hash(h, x <= -0x7fffff), h = hash(h, u > 0xfffffff0u), h = hash(h, u < 0x80000000u);
		h = hash(h, x == INT_MIN), h = hash(h, u == 0xffffffffu);
	}
	for (unsigned k = 0; k < N(lv); k++) {
		volatile long vx = lv[k];
		long x = vx;
		unsigned long u = (unsigned long)x;

		h = hash(h, x / 2), h = hash(h, x / 16), h = hash(h, x / ((long)1 << 40)), h = hash(h, x / ((long)1 << 62));
		h = hash(h, x % 2), h = hash(h, x % 32), h = hash(h, x % ((long)1 << 40));
		h = hash(h, (long)(u / 4)), h = hash(h, (long)(u % 1024)), h = hash(h, (long)(u / ((unsigned long)1 << 63)));
		h = hash(h, (long)(u * 3)), h = hash(h, (long)(u * 33)), h = hash(h, (long)(u * (((unsigned long)1 << 32) + 1)));
		h = hash(h, (x >> 4) * 5);
		h = hash(h, x > 0x100000000), h = hash(h, x < -0x1000), h = hash(h, x == (long)1 << 40), h = hash(h, u > 4096);
		h = hash(h, x == LONG_MIN), h = hash(h, x >= -4095);
	}
	{
		volatile int va = 7, vb = -3, vc = 1000;
		volatile long la = 1L << 33, lb = -5, lc = 12345;
		int a = va, b = vb, cc = vc;
		long p = la, q = lb, r = lc;

		h = hash(h, cc + a * b), h = hash(h, cc - a * b), h = hash(h, a * b + cc), h = hash(h, a - b * cc);
		h = hash(h, r + p * q), h = hash(h, r - p * q), h = hash(h, p * q - r), h = hash(h, (a * b) + (b * cc));
		h = hash(h, a + a * a), h = hash(h, a - a * b);
	}
	{
		static struct s3 a3[10];
		static struct s12 a12[10];
		static struct s24 a24[10];
		volatile int vi = 4, vj = 7;
		int i = vi, j = vj;

		for (int k = 0; k < 10; k++)
			a3[k].c[1] = (char)k, a12[k].b = k * 10, a24[k].c = k * 100;
		h = hash(h, a3[i].c[1]), h = hash(h, a12[j].b), h = hash(h, a24[i].c);
		h = hash(h, (&a12[9] - j)->b), h = hash(h, (&a24[9] - i)->c), h = hash(h, (&a3[9] - (j - i))->c[1]);
		h = hash(h, (char *)&a24[j] - (char *)&a24[i]);
	}
	printf("%lx\n", h);
	return 0;
}
