/* The loop vectorizer on 8-byte lanes (f64x2, i64x2; VIR at -O2): as vir_vec_loops.c, every loop
   at the trip counts 0-9 and 37, the arrays and the result printed, clang's scalar result the
   reference.  Two lanes change every constant of the transformation (a remainder of 0-1, a
   dependence of 1 iteration is the only short one, 8-byte strides, no 64-bit multiply), and a
   loop that mixes 4- and 8-byte data must stay scalar. */
#include <stdio.h>
#define NI __attribute__((noinline))
#define N 64

double a[N], b[N], c[N];
long la[N], lb[N], lc[N];
float fa[N];
int ia[N];
double gs = 2.5;

static void init(void)
{
	for (int i = 0; i < N; i++) {
		a[i] = i * 0.5 + 1, b[i] = (N - i) * 0.25, c[i] = (i % 7) - 3.0;
		la[i] = (long)i * 3000000000L - 20, lb[i] = 100 - (long)i * 7, lc[i] = i ^ 0x5555555555L;
		fa[i] = (float)i, ia[i] = i * 2;
	}
}

static void dump(const char *name, int n, long r)
{
	double s = 0, t = 0;
	long u = 0;

	for (int i = 0; i < N; i++)
		s += a[i] * (i + 1), t += c[i] * (i + 1) + fa[i], u += la[i] % 1000003 * (i + 1) + lb[i] + ia[i];
	printf("%s %d: %.6f %.6f %ld %ld\n", name, n, s, t, u, r);
}

NI long d_add(int n) { for (int i = 0; i < n; i++) a[i + 8] = b[i + 8] + c[i + 8]; return 0; }
NI long d_ops(int n) { for (int i = 0; i < n; i++) a[i + 8] = (b[i + 8] * c[i + 8] - a[i + 8]) / (c[i + 8] * c[i + 8] + 1) + gs; return 0; }
NI long d_down(int n) { for (int i = n - 1; i >= 0; i--) a[i + 8] = b[i + 8] * 3; return 0; }
NI long d_rev(int n) { for (int i = 0; i < n; i++) a[i + 8] = b[50 - i]; return 0; }
#define DIST(K, nm) NI long nm(int n) { for (int i = 0; i < n; i++) a[i + 8] = a[i + 8 + (K)] + b[i + 8]; return 0; }
DIST(-3, d_dm3) DIST(-2, d_dm2) DIST(-1, d_dm1) DIST(1, d_dp1) DIST(2, d_dp2)
NI long d_stst(int n) { for (int i = 0; i < n; i++) { a[i + 9] = b[i + 8]; a[i + 8] = c[i + 8]; } return 0; }
NI long d_stst2(int n) { for (int i = 0; i < n; i++) { a[i + 8] = b[i + 8]; a[i + 9] = c[i + 8]; } return 0; }
NI long d_sel(int n) { for (int i = 0; i < n; i++) a[i + 8] = b[i + 8] > c[i + 8] * 4 ? b[i + 8] : -c[i + 8]; return 0; }
NI long d_fsum(int n) { double s = 0; for (int i = 0; i < n; i++) s += b[i + 8] * 0.1; return (long)(s * 1000); }
NI long d_param(double *p, const double *q, int n) { for (int i = 0; i < n; i++) p[i] = q[i] * 0.5 + 1; return 0; }
NI long d_size(unsigned long n) { for (unsigned long i = 0; i < n; i++) a[i] = b[i] - c[i]; return 0; }
NI long d_mixf(int n) { for (int i = 0; i < n; i++) a[i + 8] = (double)fa[i + 8] + b[i + 8]; return 0; }	/* (float and double: scalar) */
NI long d_mixi(int n) { for (int i = 0; i < n; i++) a[i + 8] = b[i + 8] * (double)i; return 0; }		/* (the int index as data: scalar) */
NI long d_mixs(int n) { for (int i = 0; i < n; i++) { a[i + 8] = b[i + 8]; fa[i + 8] = 1; } return 0; }	/* (two store widths: scalar) */
NI long l_add(int n) { for (int i = 0; i < n; i++) la[i + 8] = lb[i + 8] + lc[i + 8] - (lb[i + 8] & 0xff00); return 0; }
NI long l_bits(int n) { for (int i = 0; i < n; i++) la[i + 8] = (lb[i + 8] ^ lc[i + 8]) | ~lc[i + 8]; return 0; }
NI long l_neg(int n) { for (int i = 0; i < n; i++) la[i + 8] = -lb[i + 8]; return 0; }
NI long l_mul(int n) { for (int i = 0; i < n; i++) la[i + 8] = lb[i + 8] * lc[i + 8]; return 0; }	/* (no 64-bit multiply: scalar) */
NI long l_iota(long n) { for (long i = 0; i < n; i++) la[i + 8] = i * 5 - 2; return 0; }
NI long l_iota1(long n) { for (long i = 0; i < n; i++) la[i + 8] = i + lb[i + 8]; return 0; }
NI long l_sum(int n) { long s = 7; for (int i = 0; i < n; i++) s += lb[i + 8] ^ lc[i + 8]; return s; }
NI long l_sum2(int n) { long s = 0, t = 1; for (int i = 0; i < n; i++) { s += lb[i + 8]; t -= lc[i + 8]; la[i + 8] = lb[i + 8]; } return s * 3 + t; }
NI long l_sel(int n) { for (int i = 0; i < n; i++) la[i + 8] = lb[i + 8] < lc[i + 8] ? lc[i + 8] : lb[i + 8] + 9; return 0; }
NI long l_usel(int n) { for (int i = 0; i < n; i++) la[i + 8] = (unsigned long)lb[i + 8] >= (unsigned long)la[i + 8] ? 1 : lc[i + 8]; return 0; }
NI long l_tod(long n) { for (long i = 0; i < n; i++) a[i + 8] = (double)lb[i + 8] + (double)i; return 0; }
NI long l_down(int n) { for (int i = n; i > 0; i--) la[i + 8] = lb[i + 7] + lc[i + 9]; return 0; }
/* more f64x2 values live than registers */
double gv[26] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26 };
NI long d_spill(int n)
{
	for (int i = 0; i < n; i++) {
		double x = b[i + 8], y = c[i + 8];

		a[i + 8] = (x * gv[0] + y * gv[1]) * gv[2] + (x * gv[3] - y * gv[4]) * gv[5] + (x * gv[6] + y * gv[7]) * gv[8] +
			   (x * gv[9] - y * gv[10]) * gv[11] + (x * gv[12] + y * gv[13]) * gv[14] + (x * gv[15] - y * gv[16]) * gv[17] +
			   (x * gv[18] + y * gv[19]) * gv[20] + (x * gv[21] - y * gv[22]) * gv[23] + (x * gv[24] + y * gv[25]);
	}
	return 0;
}

/* selects the if-conversion makes, on 64-bit lanes */
#define FSEL(nm, OP) NI long nm(int n) { for (int i = 0; i < n; i++) { double x = b[i + 8], y = c[i + 8] * 4; a[i + 8] = x OP y ? x : y; } return 0; }
FSEL(s_flt, <) FSEL(s_fle, <=) FSEL(s_fgt, >) FSEL(s_fge, >=) FSEL(s_feq, ==) FSEL(s_fne, !=)
#define ISEL(nm, T, OP) NI long nm(int n) { for (int i = 0; i < n; i++) { T x = (T)lb[i + 8], y = (T)lc[i + 8] - 50; la[i + 8] = x OP y ? (long)x : (long)y + 1; } return 0; }
ISEL(s_ilt, long, <) ISEL(s_ile, long, <=) ISEL(s_igt, long, >) ISEL(s_ige, long, >=) ISEL(s_ieq, long, ==) ISEL(s_ine, long, !=)
ISEL(s_ult, unsigned long, <) ISEL(s_ule, unsigned long, <=) ISEL(s_ugt, unsigned long, >) ISEL(s_uge, unsigned long, >=)
NI long s_fi(int n) { for (int i = 0; i < n; i++) { long x = lb[i + 8], y = lc[i + 8]; la[i + 8] = b[i + 8] > 9 ? x : y; } return 0; }
NI long s_nan(int n) { b[10] = __builtin_nan(""); c[13] = __builtin_nan(""); for (int i = 0; i < n; i++) { double x = b[i + 8], y = c[i + 8]; a[i + 8] = x < y ? 1.0 : x != y ? 2.0 : 3.0; } b[10] = c[13] = 0; return 0; }

static long r_param(int n) { return d_param(a + 8, b + 8, n); }
static long r_param2(int n) { return d_param(a + 9, a + 8, n); }	/* (overlapping: the scalar order) */
static long r_param3(int n) { return d_param(a + 8, a + 9, n); }
static long r_size(int n) { return d_size((unsigned long)n); }
static long r_iota(int n) { return l_iota(n); }
static long r_iota1(int n) { return l_iota1(n); }
static long r_tod(int n) { return l_tod(n); }

int main(void)
{
	static const struct { const char *name; long (*f)(int); } t[] = {
#define T(f) { #f, f }
		T(d_add), T(d_ops), T(d_down), T(d_rev), T(d_dm3), T(d_dm2), T(d_dm1), T(d_dp1), T(d_dp2), T(d_stst), T(d_stst2),
		T(d_sel), T(d_fsum), T(r_param), T(r_param2), T(r_param3), T(r_size), T(d_mixf), T(d_mixi), T(d_mixs), T(l_add),
		T(l_bits), T(l_neg), T(l_mul), T(r_iota), T(r_iota1), T(l_sum), T(l_sum2), T(l_sel), T(l_usel), T(r_tod), T(l_down),
		T(d_spill), T(s_flt), T(s_fle), T(s_fgt), T(s_fge), T(s_feq), T(s_fne), T(s_ilt), T(s_ile), T(s_igt), T(s_ige), T(s_ieq),
		T(s_ine), T(s_ult), T(s_ule), T(s_ugt), T(s_uge), T(s_fi), T(s_nan),
	};
	static const int ns[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 37 };

	for (unsigned k = 0; k < sizeof t / sizeof *t; k++)
		for (unsigned j = 0; j < sizeof ns / sizeof *ns; j++) {
			init();
			dump(t[k].name, ns[j], t[k].f(ns[j]));
		}
	return 0;
}
