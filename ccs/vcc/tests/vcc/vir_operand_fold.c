/* Operand folding (VIR at -O2, the record clean-up): register-offset loads and
   stores ([x, x, lsl #k], [x, w, sxtw #k], [x, w, uxtw #k]), shifted and extended
   operands of add/sub/logical ops and compares, madd/msub/smaddl/umaddl,
   ubfx/sbfx/ubfiz/sbfiz, products by constants as shift-add.  Edge cases: negative
   and large indices, every access width and signed loads, a store of the index
   itself, fields at bit 31 and 63, constants around 2^31. */
#define NI __attribute__((noinline))
typedef unsigned long long U;
typedef long long S;

static volatile int sink;

NI S ld64i(const S *p, int i) { return p[i]; }
NI S ld64l(const S *p, long i) { return p[i]; }
NI unsigned ld32u(const unsigned *p, unsigned i) { return p[i]; }
NI int ld16s(const short *p, int i) { return p[i]; }
NI unsigned ld16u(const unsigned short *p, int i) { return p[i]; }
NI int ld8s(const signed char *p, int i) { return p[i]; }
NI unsigned ld8u(const unsigned char *p, unsigned i) { return p[i]; }
NI S ld32sx(const int *p, int i) { return p[i]; }	/* ldrsw */
NI void st64(S *p, long i) { p[i] = i; }		/* the index stored too */
NI void st16(short *p, int i, short v) { p[i] = v; }
NI void st8(unsigned char *p, unsigned i, unsigned v) { p[i] = (unsigned char)v; }
NI int vld(volatile int *p, int i) { return p[i] + p[i]; }

NI S addsh(S a, S b) { return a + (b << 3); }
NI S subsh(S a, S b) { return a - (b >> 5); }
NI S subas(S a, S b) { return a - (b >> 63); }
NI U orsh(U a, U b) { return a | (b << 63); }
NI U eorsh(U a, U b) { return (b >> 7) ^ a; }
NI S addsx(S a, int b) { return a + b; }
NI S addux(S a, unsigned b) { return a + b; }
NI S addsx4(S a, int b) { return a + ((S)b << 4); }
NI S addsx5(S a, int b) { return a + ((S)b << 5); }	/* beyond the extended form's shift */
NI unsigned addb(unsigned a, unsigned char b) { return a + b; }
NI int subh(int a, short b) { return a - b; }
NI int cmpsh(int a, int b) { return a < (b << 2); }
NI int cmpsx(S a, int b) { return a == b; }

NI S madd(S a, S b, S c) { return a * b + c; }
NI S msub(S a, S b, S c) { return c - a * b; }
NI S smaddl(int a, int b, S c) { return (S)a * b + c; }
NI U umaddl(unsigned a, unsigned b, U c) { return (U)a * b + c; }
NI S smull(int a, int b) { return (S)a * b; }
NI S smaddk(int a, S c) { return (S)a * 24 + c; }
NI U umaddk(unsigned a, U c) { return (U)a * 0x80000001u + c; }
NI S smaddkbig(int a, S c) { return (S)a * 0x80000001ll + c; }	/* not a word */

NI S mulk(S a, int k)
{
	switch (k) {
	case 0: return a * 3;
	case 1: return a * 5;
	case 2: return a * 7;
	case 3: return a * 9;
	case 4: return a * 15;
	case 5: return a * 24;
	case 6: return a * 40;
	case 7: return a * 100;
	case 8: return a * -3;
	case 9: return a * 0x100000001ll;
	default: return a * 0x7fffffffffffffffll;
	}
}
NI int mulk32(int a) { return a * 9 + (a * 63 ^ a * 80); }
NI int neg32(int a, int f) { return f ? a * -1 : a; }	/* 2^32 - 1 in a word: neg */
NI int mulbig32(int a) { return a * 0x7fffffff; }	/* 2^31 - 1 */
NI S mulbig64(S a) { return a * 0x7fffffffffffffffll; }

NI unsigned ubfx(unsigned x) { return (x >> 5) & 0x3f; }
NI unsigned ubfx31(unsigned x) { return (x >> 27) & 0x1f; }
NI U ubfx63(U x) { return (x >> 60) & 0xf; }
NI int asrmask(int x) { return (x >> 28) & 0xff; }	/* sign copies in the mask: not ubfx */
NI int sbfx(int x) { return (x << 24) >> 28; }
NI unsigned ubfx2(unsigned x) { return (x << 3) >> 30; }
NI unsigned ubfiz(unsigned x) { return (x & 0xff) << 4; }
NI unsigned ubfizw(unsigned x) { return (x & 0xfff) << 24; }	/* the field runs off the top */
NI S sbfiz(int x) { return (S)x << 4; }
NI S sbfiz40(int x) { return (S)x << 40; }
NI U ubfizw2(unsigned x) { return (U)x << 33; }

/* a value read again after the shift: the shift must stay */
NI S twice(S a, S b, S *o)
{
	S t = b << 3;

	*o = t;
	return a + t;
}

/* a loop over an array of structs with an int index */
struct R { int k; short h; signed char c; S v; };
NI S walk(const struct R *r, int n)
{
	S s = 0;

	for (int i = 0; i < n; i++)
		s += r[i].k * 3 + r[i].h + r[i].c + r[i].v;
	return s;
}

int main(void)
{
	S a64[4] = { 10, -20, 30, 1ll << 40 };
	unsigned a32[4] = { 1, 0x80000000u, 3, 0xffffffffu };
	short a16[4] = { -1, 2, -32768, 4 };
	signed char a8[4] = { -128, 1, 127, -2 };
	int ai[4] = { -5, 6, -2147483647 - 1, 7 };
	static unsigned char big[70000];
	S st[4] = { 0 };
	unsigned char b8[4] = { 0 };
	short s16[4] = { 0 };
	volatile int vi[4] = { 1, 2, 3, 4 };

	if (ld64i(a64 + 2, -2) != 10 || ld64i(a64, 3) != 1ll << 40 || ld64l(a64 + 3, -1) != 30) return 1;
	if (ld32u(a32, 1) != 0x80000000u || ld32u(a32, 3) != 0xffffffffu) return 2;
	if (ld16s(a16 + 3, -1) != -32768 || ld16u((unsigned short *)a16, 0) != 0xffff) return 3;
	if (ld8s(a8, 0) != -128 || ld8s(a8 + 3, -3) != -128 || ld8u((unsigned char *)a8, 3) != 0xfe) return 4;
	big[69999] = 9;
	if (ld8u(big, 69999) != 9 || ld32sx(ai, 2) != -2147483647 - 1 || ld32sx(ai + 2, -2) != -5) return 5;
	st64(st, 3), st16(s16 + 2, -1, -7), st8(b8, 2, 0x1ff);
	if (st[3] != 3 || s16[1] != -7 || b8[2] != 0xff || vld(vi, 2) != 6) return 6;

	if (addsh(1, -2) != -15 || subsh(100, 64) != 98 || subas(5, -1) != 6 || subas(5, 1) != 5) return 7;
	if (orsh(1, 3) != 0x8000000000000001ull || eorsh(1, 0x80) != 0) return 8;
	if (addsx(10, -3) != 7 || addux(10, 0xffffffffu) != 0x100000009ll || addsx4(0, -1) != -16) return 9;
	if (addsx5(1, -1) != -31 || addb(1, 255) != 256 || subh(0, -32768) != 32768) return 10;
	if (!cmpsh(-9, -2) || cmpsh(8, 2) || !cmpsx(-1, -1) || cmpsx(0xffffffffll, -1)) return 11;

	if (madd(3, -4, 5) != -7 || msub(3, 4, 5) != -7) return 12;
	if (smaddl(-2147483647 - 1, 2, 1) != -4294967295ll || smull(-3, 70000) != -210000) return 13;
	if (umaddl(0xffffffffu, 0xffffffffu, 1) != 0xfffffffe00000002ull) return 14;
	if (smaddk(-1, 0) != -24 || smaddk(-2147483647 - 1, 24) != -51539607528ll) return 15;
	if (umaddk(0xffffffffu, 0) != 0xffffffffull * 0x80000001ull || smaddkbig(-1, 0) != -0x80000001ll) return 16;

	{
		static const S want[] = { 3, 5, 7, 9, 15, 24, 40, 100, -3, 0x100000001ll, 0x7fffffffffffffffll };

		for (int k = 0; k <= 10; k++)
			if (mulk(-7, k) != (S)((U)-7 * (U)want[k]))
				return 17;
		if (mulk32(-3) != -27 + (-189 ^ -240)) return 18;
		if (neg32(5, 1) != -5 || neg32(5, 0) != 5 || mulbig32(3) != (int)(3u * 0x7fffffffu) || mulbig64(-2) != 2) return 18;
	}

	if (ubfx(0xffffffffu) != 0x3f || ubfx31(0xf8000000u) != 0x1f || ubfx63(0xf000000000000000ull) != 0xf) return 19;
	if (asrmask(-0x10000000) != 0xff || asrmask(0x70000000) != 7) return 20;
	if (sbfx(0xf0) != -1 || sbfx(0x70) != 7 || ubfx2(0x18000000) != 3) return 21;
	if (ubfiz(0x1ff) != 0xff0 || ubfizw(0xabc) != 0xbc000000u) return 22;
	if (sbfiz(-1) != -16 || sbfiz40(-1) != -(1ll << 40) || ubfizw2(0xffffffffu) != 0xfffffffe00000000ull) return 23;
	{
		S o = 0;

		if (twice(1, 2, &o) != 17 || o != 16) return 24;
	}
	{
		struct R r[3] = { { 1, -2, -3, 100 }, { 2, 3, 4, -5 }, { -1, 0, 127, 1ll << 33 } };

		if (walk(r, 3) != (3 - 2 - 3 + 100) + (6 + 3 + 4 - 5) + (-3 + 0 + 127 + (1ll << 33))) return 25;
	}
	sink = 0;
	return 0;
}
