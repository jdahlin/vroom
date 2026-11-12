/* memcpy of 1, 2, 4 or 8 bytes (VIR at -O2: one load and one store, so the locals it copies
   between can live in registers): type punning, a record of two ints, a loop on the copy. */
#include <string.h>

typedef unsigned long long u64;
struct pair { int a, b; };

__attribute__((noinline)) static int digits(double r)
{
	u64 v;
	int n = 0;

	memcpy(&v, &r, 8);
	v >>= 40;
	while (v) {
		n += (int)(v % 10);
		v /= 10;
	}
	return n;
}

__attribute__((noinline)) static unsigned fbits(float f)
{
	unsigned u;

	memcpy(&u, &f, sizeof u);
	return u;
}

__attribute__((noinline)) static int pairsum(const struct pair *p)
{
	struct pair q;

	memcpy(&q, p, sizeof q);
	return q.a * 10 + q.b;
}

__attribute__((noinline)) static int bytes(const unsigned char *p)
{
	unsigned short h;
	unsigned char c;

	memcpy(&h, p + 1, 2);
	memcpy(&c, p, 1);
	return h + c;
}

int main(void)
{
	struct pair p = { 4, 7 };
	unsigned char b[4] = { 1, 2, 3, 4 };
	double d = 1.5;
	u64 x;

	memcpy(&x, &d, 8);
	if (x != 0x3ff8000000000000ull) return 1;
	if (digits(1.5) != 29) return 2;	/* 0x3ff800 = 4192256 */
	if (fbits(1.0f) != 0x3f800000u) return 3;
	if (pairsum(&p) != 47) return 4;
	if (bytes(b) != 0x0302 + 1) return 5;
	return 0;
}
