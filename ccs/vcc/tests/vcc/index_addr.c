/* Array accesses with a variable index (VIR at -O2 on arm64: [base, index] with the index's
   sign or zero extension and the scaling by the element size folded): every width, signed,
   unsigned and 64-bit indexes, negative ones, loads and stores, floating point. */
#define NI __attribute__((noinline))
typedef long long I;

NI int ld32s(const int *p, int i) { return p[i]; }
NI int ld32u(const int *p, unsigned i) { return p[i]; }
NI I ld64(const I *p, I i) { return p[i]; }
NI int ld8(const signed char *p, int i) { return p[i]; }
NI unsigned ld8u(const unsigned char *p, unsigned i) { return p[i]; }
NI int ld16(const short *p, int i) { return p[i]; }
NI I ld32w(const int *p, int i) { return p[i]; }	/* ldrsw */
NI double ldd(const double *p, int i) { return p[i]; }
NI float ldf(const float *p, unsigned i) { return p[i]; }
NI void st32(int *p, int i, int v) { p[i] = v; }
NI void st16(short *p, unsigned i, int v) { p[i] = (short)v; }
NI void st8(char *p, I i, int v) { p[i] = (char)v; }
NI void std_(double *p, int i, double v) { p[i] = v; }
NI void st64(I *p, int i, I v) { p[i] = v; }
NI int sum(const int *p, int n) { int s = 0; for (int i = 0; i < n; i++) s += p[i] * (i + 1); return s; }
NI void fill(short *p, int n) { for (int i = n - 1; i >= 0; i--) p[i] = (short)(i * 3 - 7); }

int main(void)
{
	int a[8] = { 1, -2, 3, -4, 5, -6, 7, -8 };
	I q[4] = { 10, -20, 30, -40 };
	signed char c[4] = { -1, 2, -3, 4 };
	short h[6] = { -100, 200, -300, 400, 0, 0 };
	double d[3] = { 1.5, -2.5, 3.25 };
	float f[3] = { 0.5f, -0.25f, 8.0f };

	if (ld32s(a, 3) != -4 || ld32s(a + 4, -2) != 3 || ld32u(a, 7) != -8) return 1;
	if (ld64(q, 2) != 30 || ld64(q + 3, -1) != 30) return 2;
	if (ld8(c, 2) != -3 || ld8(c + 2, -1) != 2 || ld8u((unsigned char *)c, 0) != 255) return 3;
	if (ld16(h, 3) != 400 || ld16(h + 2, -2) != -100) return 4;
	if (ld32w(a, 5) != -6 || ld32w(a + 6, -5) != -2) return 5;
	if (ldd(d, 2) != 3.25 || ldd(d + 2, -1) != -2.5 || ldf(f, 1) != -0.25f) return 6;
	st32(a, 1, 99); st32(a + 4, -4, 77);
	if (a[1] != 99 || a[0] != 77) return 7;
	st16(h, 4, -5); st8((char *)c, 3, 9); std_(d, 1, 0.125); st64(q, 3, -1);
	if (h[4] != -5 || c[3] != 9 || d[1] != 0.125 || q[3] != -1) return 8;
	if (sum(a, 8) != 77 + 2 * 99 + 3 * 3 + 4 * -4 + 5 * 5 + 6 * -6 + 7 * 7 + 8 * -8) return 9;
	fill(h, 6);
	if (h[0] != -7 || h[5] != 8) return 10;
	return 0;
}
