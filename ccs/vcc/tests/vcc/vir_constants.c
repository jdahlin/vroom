/* Constants held by their reader (VIR at -O2, the record clean-up): a stored zero
   is wzr/xzr, an add/sub/compare takes a 12-bit immediate (maybe shifted by 12, of
   either sign), a logical op an encodable mask.  Edge cases: a constant whose low
   bytes are zero stored narrow, 0x80000000 and -1 in a word, 4095/4096/0x1000000,
   masks that are not logical immediates, a constant two readers share. */
#define NI __attribute__((noinline))
typedef unsigned long long U;
typedef long long S;

static volatile int sink;

struct T { S a; int b; short c; signed char d; S e[4]; };

NI void zero(struct T *t, int i)
{
	t->a = 0, t->b = 0, t->c = 0, t->d = 0;
	t->e[i] = 0;
}
NI void narrow(unsigned char *p, unsigned short *q) { *p = (unsigned char)0x100; *q = (unsigned short)0x30000; }
NI void slots(S *o, int n)	/* zero kept in a slot across a call, stored back */
{
	S z = 0, w = 0;

	for (int i = 0; i < n; i++) {
		sink = i;
		z += i, w ^= i;
	}
	o[0] = z, o[1] = w;
}

NI int cmps(int x)
{
	return (x == 4095) + 2 * (x == -4095) + 4 * (x > 4096) + 8 * (x < -4096) + 16 * (x == 0x7ff000) + 32 * (x == (int)0x80000000);
}
NI int cmpx(S x) { return (x == -1) + 2 * (x < 0x1000000) + 4 * (x == 0xfff000) + 8 * (x == -0xfff000); }
NI S adds(S x) { return (x + 4095) ^ (x - 4096) ^ (x + 0xabc000) ^ (x - 0x1001); }
NI int addw(int x) { return (int)((unsigned)((x + -7) * (x - -9)) + ((unsigned)x + 0x7fffffffu)); }
NI unsigned logic(unsigned x) { return (x & 0xff00ff00) | ((x ^ 0x0f0f0f0f) & 0x12345678) | (x | 0x80000001); }
NI U logicx(U x) { return (x & 0xffffffff00000000ull) ^ (x | 0x5555555555555555ull) ^ (x & 0x123456789ull); }
NI int tst(U x) { return (x & 0x00ff000000000000ull) != 0; }
NI S shared(S x, S *o) { S k = 77; *o = x + k; return x * k; }

int main(void)
{
	struct T t = { 1, 2, 3, 4, { 5, 6, 7, 8 } };
	unsigned char b = 9;
	unsigned short h = 9;
	S o[2] = { 1, 1 };

	zero(&t, 2);
	if (t.a || t.b || t.c || t.d || t.e[2] || t.e[1] != 6 || t.e[3] != 8) return 1;
	narrow(&b, &h);
	if (b || h) return 2;
	slots(o, 5);
	if (o[0] != 10 || o[1] != (0 ^ 1 ^ 2 ^ 3 ^ 4)) return 3;
	if (cmps(4095) != 1 || cmps(-4095) != 2 || cmps(5000) != 4 || cmps(-5000) != 8) return 4;
	if (cmps(0x7ff000) != 4 + 16 || cmps((int)0x80000000) != 8 + 32 || cmps(4096) != 0) return 5;
	if (cmpx(-1) != 3 || cmpx(0xfff000) != 6 || cmpx(-0xfff000) != 10 || cmpx(0x1000000) != 0) return 6;
	{
		S x = 12345;

		if (adds(x) != ((x + 4095) ^ (x - 4096) ^ (x + 0xabc000) ^ (x - 0x1001))) return 7;
		if (addw(3) != (int)((unsigned)((3 - 7) * (3 + 9)) + 3u + 0x7fffffffu)) return 8;
	}
	if (logic(0xdeadbeefu) != ((0xdeadbeefu & 0xff00ff00) | ((0xdeadbeefu ^ 0x0f0f0f0f) & 0x12345678) | (0xdeadbeefu | 0x80000001)))
		return 9;
	{
		U x = 0x0123456789abcdefull;

		if (logicx(x) != ((x & 0xffffffff00000000ull) ^ (x | 0x5555555555555555ull) ^ (x & 0x123456789ull))) return 10;
	}
	if (!tst(0x0001000000000000ull) || tst(0x0100000000000000ull)) return 11;
	if (shared(3, o) != 231 || o[0] != 80) return 12;
	return 0;
}
