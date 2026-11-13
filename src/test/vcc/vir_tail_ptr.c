/* VIR lowering: a sibling call whose result is returned as a pointer (the ret names the
 * call's value under its i2p), through casts, a join and a block parameter; and leaf
 * functions made of FP instructions, which need no frame record. */
#include <stdio.h>
#define NI __attribute__((noinline))

struct M { long a; char *z; unsigned short flags; unsigned char enc; };

NI static const void *text(struct M *p, unsigned char enc) { return p->z + enc; }
NI static void *pick(void *a, void *b, long n) { return n & 1 ? a : b; }
NI static long num(struct M *p) { return p->a * 3; }

NI static const void *value(struct M *p, unsigned char enc)
{
	if (!p)
		return 0;
	if ((p->flags & 0x202) == 0x202 && p->enc == enc)
		return p->z;
	if (p->flags & 1)
		return 0;
	return text(p, enc);
}

NI static const unsigned char *bytes(struct M *p) { return (const unsigned char *)value(p, 1); }
NI static char *either(char *a, char *b, long n) { return n > 10 ? (char *)pick(b, a, n) : (char *)pick(a, b, n); }
NI static long asnum(struct M *p) { return p ? (long)(unsigned long)value(p, 2) + num(p) : -1; }
NI static void *chain(struct M *p, long n)
{
	void *r;

	if (n == 0)
		r = pick(p, 0, 1);
	else if (n == 1)
		r = (void *)value(p, 3);
	else
		r = p->z;
	return r;
}

NI static double fleaf(double a, double b, long n) { return a * b + (double)n - a / (b + 1.5); }
NI static long ileaf(double a) { return a > 2.5 ? (long)a : a < -2.5 ? -(long)-a : 0; }

int main(void)
{
	static char buf[32] = "abcdefghijklmnopqrstuvwxyz";
	struct M m[4] = { { 5, buf, 0x202, 1 }, { 6, buf + 4, 0x202, 9 }, { 7, buf + 8, 1, 1 }, { 8, buf + 12, 0, 1 } };

	printf("%d\n", bytes(0) == 0);
	for (int i = 0; i < 4; i++) {
		const unsigned char *b = bytes(&m[i]);

		printf("%d %ld %c %c ", b ? (int)(b - (const unsigned char *)buf) : -1, asnum(&m[i]) - (value(&m[i], 2) ? (long)(unsigned long)buf : 0),
		       *either(buf + i, buf + 20, i), *either(buf + i, buf + 20, 11 + i));
		for (long n = 0; n < 3; n++) {
			char *c = chain(&m[i], n);

			printf("%d ", !c ? -1 : c >= buf && c < buf + 32 ? (int)(c - buf) : 1000 + (int)((struct M *)c - m));
		}
		printf("\n");
	}
	printf("%g %g %ld %ld %ld\n", fleaf(1.5, 2.5, 3), fleaf(-4, 0.25, -7), ileaf(7.9), ileaf(-7.9), ileaf(1));
	return 0;
}
