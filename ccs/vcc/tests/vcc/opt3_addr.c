/* vcc-flags: -O3 */
/*
 * -O3 addressing (value.c fold_index, mem_far): indexed loads and stores
 * of every size, signed and unsigned, through int, unsigned, long and
 * char indexes (sxtw/uxtw/lsl forms), negative indexes, register
 * variables used as index (and updated in the index expression), stores of
 * zero, and fields far into a large struct.
 */
#include <stdio.h>

static unsigned long h;
__attribute__((noinline)) static void mix(long v) { h = h * 31 + (unsigned long)v; }

static signed char sc[64];
static unsigned char uc[64];
static short ss[64];
static unsigned short us[64];
static int si[64];
static unsigned ui[64];
static long sl[64];
static float fl[64];
static double db[64];

struct big {
	char pad[5000];
	int a;
	char pad2[70000];
	long b;
	short c;
	char d;
	char pad3[1 << 20];
	long e;
};
static struct big bg;

__attribute__((noinline)) static void loads(int i, unsigned u, long l, signed char c)
{
	mix(sc[i]), mix(uc[i]), mix(ss[i]), mix(us[i]), mix(si[i]), mix(ui[i]), mix(sl[i]);
	mix(sc[u]), mix(uc[u]), mix(ss[u]), mix(us[u]), mix(si[u]), mix(ui[u]), mix(sl[u]);
	mix(sc[l]), mix(uc[l]), mix(ss[l]), mix(us[l]), mix(si[l]), mix(ui[l]), mix(sl[l]);
	mix(sc[c]), mix(ss[c]), mix(si[c]), mix(sl[c]);
	mix((long)fl[i]), mix((long)db[u]), mix((long)db[l]);
	mix(sl[i + 1]), mix(si[u - 1]), mix(ss[l * 2 % 63]);
	{
		int *p = si + 32;

		mix(p[-i % 32]), mix(p[(i - 16) % 32]);
	}
}

__attribute__((noinline)) static void stores(int i, unsigned u, long l, long v)
{
	sc[i] = (signed char)v, uc[u] = (unsigned char)v, ss[l] = (short)v, us[i] = (unsigned short)v;
	si[u] = (int)v, ui[l] = (unsigned)v, sl[i] = v, fl[u] = (float)v, db[l] = (double)v;
	sl[u + 1] = 0, si[l ^ 1] = 0, sc[i ^ 2] = 0, ss[u ^ 3] = 0;
}

__attribute__((noinline)) static long regidx(long n)
{
	register long k = n;
	long s = 0;

	for (int j = 0; j < 8; j++) {
		s += sl[(k = (int)(k + 3)) & 63];
		s += si[k & 31];
		ui[(unsigned)k & 63] = (unsigned)s;
	}
	return s + k;
}

__attribute__((noinline)) static void far(struct big *p, long v)
{
	p->a = (int)v, p->b = v * 3, p->c = (short)v, p->d = (char)v, p->e = v - 1;
	mix(p->a), mix(p->b), mix(p->c), mix(p->d), mix(p->e);
	mix(p->pad[4999] + p->pad2[69999] + p->pad3[(1 << 20) - 1]);
}

int main(void)
{
	for (int k = 0; k < 64; k++) {
		sc[k] = (signed char)(k * 7 - 200), uc[k] = (unsigned char)(k * 13);
		ss[k] = (short)(k * 1000 - 30000), us[k] = (unsigned short)(k * 999);
		si[k] = k * 0x1234567 - 0x40000000, ui[k] = 0xf0000000u + (unsigned)k * 77;
		sl[k] = (long)k * 0x123456789L - (1L << 40), fl[k] = (float)k * 1.5f, db[k] = k * -2.25;
	}
	for (int i = 1; i < 63; i++)
		loads(i, (unsigned)i, i, (signed char)(i / 2));
	for (int i = 0; i < 62; i++)
		stores(i, (unsigned)i, i, (long)i * 0x10203 - 7);
	for (int k = 0; k < 64; k++)
		mix(sc[k] + uc[k] + ss[k] + us[k] + si[k] + (long)ui[k] + sl[k] + (long)fl[k] + (long)db[k]);
	mix(regidx(5)), mix(regidx(-9));
	far(&bg, 0x7654321);
	far(&bg, -5);
	printf("%lx\n", h);
	return 0;
}
