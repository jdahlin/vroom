/* If-conversion (VIR at -O2, the builder): a branch whose arms hold nothing, or a
   couple of cheap instructions without effects, and that differ in one value at the
   join becomes select (csel/fcsel).  Edge cases that must stay branches: an arm
   that loads (p ? *p : 0, the pointer may be null), stores, calls or divides; two
   values that differ; and shapes that convert: diamond and triangle on either side,
   in a loop (min/max, a running maximum carried round the back edge), pointers,
   floats (NaN compares), unsigned and 64-bit values, nested conditions, a condition
   that is not a compare, an arm that computes from the condition's operands. */
#include <stdio.h>

#define NI __attribute__((noinline))
typedef long long S;
typedef unsigned long long U;

static volatile int sink;
NI int bump(int x) { sink++; return x + 1; }

NI int tern(int a, int b, int c) { return a < b ? b : c; }
NI int tri_t(int a, int b) { int x = a; if (b > 3) x = 7; return x * 3; }
NI int tri_f(int a, int b) { int x = a; if (!(b > 3)) x = -7; return x - b; }
NI unsigned umin(unsigned a, unsigned b) { if (a > b) a = b; return a; }
NI S smax(S a, S b) { return a > b ? a : b; }
NI U sat(U a, U b) { U s = a + b; if (s < a) s = ~0ull; return s; }
NI double fmn(double a, double b) { return a < b ? a : b; }
NI float fsel(int c, float a, float b) { return c ? a : b; }
NI const char *psel(const char *p, const char *q, int n) { return n & 4 ? p + 1 : q; }
NI int arith(int a, int b) { return a > b ? a - b : b - a; }	/* both arms compute from the operands */
NI int absx(int a) { return a < 0 ? -a : a; }
NI int flagv(int c, int a, int b) { return c ? a : b; }	/* the condition is a value */
NI int flagx(U c, int a, int b) { return c & 0x100000000ull ? a : b; }	/* ... a 64-bit one: bit 32 */
NI int nest(int a, int b, int c) { return a > 0 ? (b > 0 ? 1 : 2) : (c > 0 ? 3 : 4); }
NI int clampi(int x, int lo, int hi) { if (x < lo) x = lo; if (x > hi) x = hi; return x; }

/* must stay branches */
NI int deref(const int *p) { return p ? *p : 0; }
NI int deref2(const int *p, const int *q, int c) { return c ? p[0] : q[1]; }
NI int store(int *p, int c, int v) { int r = 1; if (c) { *p = v; r = 2; } return r; }
NI int call(int c, int v) { int r = v; if (c) r = bump(v); return r; }
NI int divz(int a, int b) { return b ? a / b : 0; }
NI int two(int c, int *o) { int x = 1, y = 2; if (c) x = 3, y = 4; *o = y; return x; }
NI int vol(int c) { int r = 0; if (c) r = sink; return r; }

NI S loopmax(const int *v, int n, int *at)
{
	int m = v[0], k = 0;
	S s = 0;

	for (int i = 1; i < n; i++) {
		if (v[i] > m)
			m = v[i];	/* one value: a select round the back edge */
		if (v[i] < 0)
			k = i;
		s += v[i] > 10 ? 10 : v[i];
	}
	*at = k;
	return s * 1000 + m;
}

int main(void)
{
	static const int v[8] = { 3, -4, 99, 12, -2147483647 - 1, 2147483647, 0, 7 };
	const char *str = "abcdef";
	int o = 0, at = 0, cell = 5;
	double nan = __builtin_nan("");
	U t = 0;

	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 8; j += 3) {
			int a = v[i], b = v[j];

			t = t * 31 + (0ull + tern(a, b, i) + tri_t(a & 0xffff, b) + tri_f(a & 0xffff, b & 0xffff) + umin((unsigned)a, (unsigned)b));
			t = t * 31 + (0ull + smax(a, b) + (S)sat((U)a << 33, (U)b << 33) + (psel(str, str + 3, a) - str) + arith(a >> 1, b >> 1));
			t = t * 31 + (0ull + absx(a >> 1) + flagv(a, b, i) + flagx((U)i << 32, a, 5) + flagx((U)(unsigned)a, 6, j) + nest(a, b, i - 4) + clampi(a, -5, 50) + clampi(b, 60, 40));
			t = t * 31 + (0ull + deref(i & 1 ? &v[j] : 0) + deref2(&v[i], &v[j], j & 1) + store(&cell, a > b, i) + cell);
			t = t * 31 + (0ull + call(a < b, j) + divz(a >> 2, b & 7) + two(a == b, &o) + o + vol(i == j));
			t = t * 31 + (0ull + (S)(fmn(a, b) + fsel(a & 1, 1.5f, 2.5f) * 2));
		}
	t = t * 31 + (0ull + (fmn(nan, 1.0) == 1.0) + 2 * (fmn(1.0, nan) != fmn(1.0, nan)) + 4 * (fmn(-0.0, 0.0) == 0.0));
	t = t * 31 + (0ull + loopmax(v, 8, &at) + at + sink);
	printf("%llu\n", t);
	return 0;
}
