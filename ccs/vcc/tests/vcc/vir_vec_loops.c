/* The loop vectorizer (VIR at -O2, emit/vir/vec.inc): every loop below is run for the trip
   counts 0-9 and 37, from offsets that leave room on both sides, and what it leaves in the
   arrays (and returns) is printed; clang's scalar result is the reference.  The cases are the
   ones a vectorizer gets wrong: a remainder of 0-3, a count below 4, a loop that runs down, two
   accesses of one array at every distance from -5 to 5 (some must stay scalar, the result must
   be the scalar one either way), a store before the load of the same statement, sums, a
   comparison picking lanes, a step other than 1, the index as data, an invariant read from
   memory, an index that would wrap. */
#include <stdio.h>
#define NI __attribute__((noinline))
#define N 64

float a[N], b[N], c[N], d[N];
int ia[N], ib[N], ic[N];
float aa[8][8];
float gs = 1.5f;
int gk = 3;

static void init(void)
{
	for (int i = 0; i < N; i++) {
		a[i] = (float)i * 0.5f + 1, b[i] = (float)(N - i) * 0.25f, c[i] = (float)(i % 7) - 3, d[i] = 0.125f * (float)i;
		ia[i] = i * 3 - 20, ib[i] = 100 - i * 7, ic[i] = i ^ 0x55;
	}
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 8; j++)
			aa[i][j] = (float)(i * 8 + j);
}

static void dump(const char *name, int n, long r)
{
	double s = 0, t = 0;
	long u = 0;

	for (int i = 0; i < N; i++)
		s += a[i] * (i + 1), t += b[i] * (i + 1), u += (long)ia[i] * (i + 1) + ib[i];
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 8; j++)
			s += aa[i][j] * (i * 9 + j + 1);
	printf("%s %d: %.6f %.6f %ld %ld\n", name, n, s, t, u, r);
}

NI long l_add(int n) { for (int i = 0; i < n; i++) a[i + 8] = b[i + 8] + c[i + 8]; return 0; }
NI long l_ops(int n) { for (int i = 0; i < n; i++) a[i + 8] = (b[i + 8] * c[i + 8] - d[i + 8]) / (c[i + 8] * c[i + 8] + 1) + gs; return 0; }
NI long l_abs(int n) { for (int i = 0; i < n; i++) a[i + 8] = __builtin_fabsf(c[i + 8]) + __builtin_sqrtf(d[i + 8]) - -b[i + 8]; return 0; }
NI long l_down(int n) { for (int i = n - 1; i >= 0; i--) a[i + 8] = b[i + 8] + 1; return 0; }
NI long l_down2(int n) { for (int i = n; i > 0; i--) a[i + 8] = b[i + 7] * c[i + 9]; return 0; }
NI long l_rev(int n) { for (int i = 0; i < n; i++) a[i + 8] = b[40 - i]; return 0; }
NI long l_le(int n) { for (int i = 2; i <= n; i++) a[i + 8] = b[i] + c[i]; return 0; }
NI long l_step2(int n) { for (int i = 0; i < n; i += 2) a[i + 8] = b[i + 8]; return 0; }
NI long l_two(int n) { for (int i = 0; i < n; i++) { a[i + 8] = c[i + 8] + 1; b[i + 8] = a[i + 8] * 2; } return 0; }
NI long l_self(int n) { for (int i = 0; i < n; i++) a[i + 8] = a[i + 8] * 2 + 1; return 0; }
/* one array at a distance: a[i+8] from a[i+8+K] */
#define DIST(K, nm) NI long nm(int n) { for (int i = 0; i < n; i++) a[i + 8] = a[i + 8 + (K)] + b[i + 8]; return 0; }
DIST(-5, l_dm5) DIST(-4, l_dm4) DIST(-3, l_dm3) DIST(-2, l_dm2) DIST(-1, l_dm1)
DIST(1, l_dp1) DIST(2, l_dp2) DIST(3, l_dp3) DIST(4, l_dp4) DIST(5, l_dp5)
/* ... running down */
#define DISTD(K, nm) NI long nm(int n) { for (int i = n - 1; i >= 0; i--) a[i + 8] = a[i + 8 + (K)] + b[i + 8]; return 0; }
DISTD(-2, l_ddm2) DISTD(-1, l_ddm1) DISTD(1, l_ddp1) DISTD(3, l_ddp3) DISTD(4, l_ddp4)
/* the store first, then a load of a later iteration's element (it must see the old value) */
NI long l_stld(int n) { for (int i = 0; i < n; i++) { a[i + 8] = b[i + 8]; c[i + 8] = a[i + 9] + 1; } return 0; }
/* ... of an earlier one (the new value) */
NI long l_stld2(int n) { for (int i = 0; i < n; i++) { a[i + 8] = b[i + 8]; c[i + 8] = a[i + 7] + 1; } return 0; }
/* two stores to one array one apart: the later statement wins where they meet */
NI long l_stst(int n) { for (int i = 0; i < n; i++) { a[i + 8] = b[i + 8]; a[i + 9] = c[i + 8]; } return 0; }
NI long l_stst2(int n) { for (int i = 0; i < n; i++) { a[i + 9] = b[i + 8]; a[i + 8] = c[i + 8]; } return 0; }
NI long l_int(int n) { for (int i = 0; i < n; i++) ia[i + 8] = (ib[i + 8] + ic[i + 8] * 3 - (ib[i + 8] & 0xf0)) ^ (ic[i + 8] | 5); return 0; }
NI long l_iota(int n) { for (int i = 0; i < n; i++) ia[i + 8] = i * 5 - 2; return 0; }
NI long l_iotaf(int n) { for (int i = 0; i < n; i++) a[i + 8] = (float)i * b[i + 8]; return 0; }
NI long l_sum(int n) { int s = 7; for (int i = 0; i < n; i++) s += ib[i + 8] * ic[i + 8]; return s; }
NI long l_sum2(int n) { int s = 0, t = 100; for (int i = 0; i < n; i++) { s += ib[i + 8]; t -= ic[i + 8]; ia[i + 8] = ib[i + 8] + 1; } return s * 1000 + t; }
NI long l_fsum(int n) { float s = 0; for (int i = 0; i < n; i++) s += b[i + 8] * 0.1f; return (long)(s * 1000); }
NI long l_sel(int n) { for (int i = 0; i < n; i++) a[i + 8] = b[i + 8] > c[i + 8] * 4 ? b[i + 8] : d[i + 8]; return 0; }
NI long l_isel(int n) { for (int i = 0; i < n; i++) ia[i + 8] = ib[i + 8] < ic[i + 8] ? ic[i + 8] - ib[i + 8] : gk; return 0; }
NI long l_inv(int n) { for (int i = 0; i < n; i++) a[i + 8] = b[i + 8] * gs + (float)gk; return 0; }
NI long l_invsame(int n) { for (int i = 0; i < n; i++) a[i + 8] = b[i + 8] * a[9]; return 0; }
NI long l_2d(int n) { for (int j = 1; j < 7; j++) for (int i = 0; i < n && i < 8; i++) aa[j][i] = aa[j - 1][i] + aa[j][i]; return 0; }
NI long l_param(float *p, float *q, int n) { for (int i = 0; i < n; i++) p[i] = q[i] + 1; return 0; }
NI long l_psum(const int *p, int n) { int s = 0; for (int i = 0; i < n; i++) s += p[i]; return s; }
NI long l_lim(int lo, int hi) { for (int i = lo; i < hi; i++) a[i] = b[i] - 1; return 0; }
NI long l_uns(unsigned n) { for (unsigned i = 0; i < n; i++) a[i + 8] = b[i + 8] + 2; return 0; }
NI long l_long(long n) { for (long i = 0; i < n; i++) a[i + 8] = b[i + 8] + 3; return 0; }
NI long l_ptr(int n) { float *p = a + 8, *q = b + 8; for (int i = 0; i < n; i++) *p++ = *q++ * 2; return 0; }
NI long l_cond(int n) { for (int i = 0; i < n; i++) if (b[i + 8] > 10) a[i + 8] = 0; return 0; }
NI long l_scal(int n) { float t = 0; for (int i = 0; i < n; i++) { t = b[i + 8] + c[i + 8]; a[i + 8] = t * t; } return (long)t; }
/* the index wraps (unsigned arithmetic is defined): 0xfffffffe + i reaches 0 */
NI long l_wrap(int n) { for (unsigned i = 0; i < (unsigned)n; i++) a[(unsigned char)(250 + i) & 31] = b[i + 8]; return 0; }

/* more vectors live across the loop than there are v registers: some are kept in 16-byte spill slots */
float gv[28] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28 };
NI long l_spill(int n)
{
	for (int i = 0; i < n; i++) {
		float x = b[i + 8], y = c[i + 8];

		a[i + 8] = ((x * gv[0] + y * gv[1]) * gv[2] + (x * gv[3] - y * gv[4]) * gv[5] + (x * gv[6] + y * gv[7]) * gv[8] +
			    (x * gv[9] - y * gv[10]) * gv[11] + (x * gv[12] + y * gv[13]) * gv[14] + (x * gv[15] - y * gv[16]) * gv[17] +
			    (x * gv[18] + y * gv[19]) * gv[20] + (x * gv[21] - y * gv[22]) * gv[23] + (x * gv[24] + y * gv[25]) * gv[26]) * gv[27];
	}
	return 0;
}

/* a step that is a value (the vector loop runs when it is 1), an index bumped twice in the body,
   one array at a distance only known when the loop is entered, unsigned and size_t counters */
NI long l_stepv(int n, int st) { for (int i = 0; i < n; i += st) a[i + 8] = b[i + 8] * 3; return 0; }
NI long l_twice(int n) { int j = 7; for (int i = 0; i < n; i++) { j++; a[j] = b[i + 8] + (float)j; j++; j--; } return j; }
NI long l_distv(int n, int k) { for (int i = 0; i < n; i++) a[i + 8] = a[i + k] + c[i + 8]; return 0; }
NI long l_uns2(unsigned n) { for (unsigned i = 0; i < n; i++) a[i] = b[i] + 2; return 0; }
NI long l_size(unsigned long lo, unsigned long hi) { for (unsigned long i = lo; i < hi; i++) a[i] = b[i] * 0.5f; return 0; }
NI long l_uwrap(unsigned n) { for (unsigned i = 4294967290u; i != n; i++) a[(i + 16) & 63] = b[i & 63]; return 0; }
static long r_stepv1(int n) { return l_stepv(n, 1); }
static long r_stepv3(int n) { return l_stepv(n, 3); }
static long r_distv9(int n) { return l_distv(n, 9); }
static long r_distv6(int n) { return l_distv(n, 6); }
static long r_distv50(int n) { return l_distv(n, 50 - n); }
static long r_uns2(int n) { return l_uns2((unsigned)n); }
static long r_size(int n) { return l_size(10, 10 + (unsigned long)n); }
static long r_size2(int n) { return l_size(30, (unsigned long)n); }	/* (lo above hi: no iteration) */
static long r_uwrap(int n) { return l_uwrap((unsigned)n); }

/* selects the if-conversion makes (both values computed before the test): every comparison */
#define FSEL(nm, OP) NI long nm(int n) { for (int i = 0; i < n; i++) { float x = b[i + 8], y = c[i + 8] * 4; a[i + 8] = x OP y ? x : y; } return 0; }
FSEL(s_flt, <) FSEL(s_fle, <=) FSEL(s_fgt, >) FSEL(s_fge, >=) FSEL(s_feq, ==) FSEL(s_fne, !=)
#define ISEL(nm, T, OP) NI long nm(int n) { for (int i = 0; i < n; i++) { T x = (T)ib[i + 8], y = (T)ic[i + 8]; ia[i + 8] = x OP y ? (int)x : (int)y + 1; } return 0; }
ISEL(s_ilt, int, <) ISEL(s_ile, int, <=) ISEL(s_igt, int, >) ISEL(s_ige, int, >=) ISEL(s_ieq, int, ==) ISEL(s_ine, int, !=)
ISEL(s_ult, unsigned, <) ISEL(s_ule, unsigned, <=) ISEL(s_ugt, unsigned, >) ISEL(s_uge, unsigned, >=)
/* a float test picking integers, an integer test picking floats, a test against the index, a NaN in the data */
NI long s_fi(int n) { for (int i = 0; i < n; i++) { int x = ib[i + 8], y = ic[i + 8]; ia[i + 8] = b[i + 8] > 9 ? x : y; } return 0; }
NI long s_if(int n) { for (int i = 0; i < n; i++) { float x = b[i + 8], y = d[i + 8]; a[i + 8] = ib[i + 8] > 40 ? x : y; } return 0; }
NI long s_idx(int n) { for (int i = 0; i < n; i++) { int x = ib[i + 8], y = ic[i + 8]; ia[i + 8] = i > 3 ? x : y; } return 0; }
NI long s_nan(int n) { b[10] = __builtin_nanf(""); c[13] = __builtin_nanf(""); for (int i = 0; i < n; i++) { float x = b[i + 8], y = c[i + 8]; a[i + 8] = x < y ? 1.0f : x != y ? 2.0f : 3.0f; } b[10] = c[13] = 0; return 0; }

static long r_param(int n) { return l_param(a + 8, b + 8, n); }
static long r_param2(int n) { return l_param(a + 9, a + 8, n); }	/* the two overlap: the scalar order shows */
static long r_param3(int n) { return l_param(a + 8, a + 9, n); }
static long r_psum(int n) { return l_psum(ib + 8, n); }
static long r_lim(int n) { return l_lim(20 - n / 2, 20 + n - n / 2); }

int main(void)
{
	static const struct { const char *name; long (*f)(int); } t[] = {
#define T(f) { #f, f }
		T(l_add), T(l_ops), T(l_abs), T(l_down), T(l_down2), T(l_rev), T(l_le), T(l_step2), T(l_two), T(l_self),
		T(l_dm5), T(l_dm4), T(l_dm3), T(l_dm2), T(l_dm1), T(l_dp1), T(l_dp2), T(l_dp3), T(l_dp4), T(l_dp5),
		T(l_ddm2), T(l_ddm1), T(l_ddp1), T(l_ddp3), T(l_ddp4), T(l_stld), T(l_stld2), T(l_stst), T(l_stst2),
		T(l_int), T(l_iota), T(l_iotaf), T(l_sum), T(l_sum2), T(l_fsum), T(l_sel), T(l_isel), T(l_inv), T(l_invsame),
		T(s_flt), T(s_fle), T(s_fgt), T(s_fge), T(s_feq), T(s_fne), T(s_ilt), T(s_ile), T(s_igt), T(s_ige), T(s_ieq), T(s_ine),
		T(s_ult), T(s_ule), T(s_ugt), T(s_uge), T(s_fi), T(s_if), T(s_idx), T(s_nan),
		T(l_2d), T(l_spill), T(r_stepv1), T(r_stepv3), T(l_twice), T(r_distv9), T(r_distv6), T(r_distv50), T(r_uns2), T(r_size),
		T(r_size2), T(r_uwrap), T(r_param), T(r_param2), T(r_param3), T(r_psum), T(r_lim), T(l_cond), T(l_scal), T(l_wrap),
	};
	static const int ns[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 37 };

	for (unsigned k = 0; k < sizeof t / sizeof *t; k++)
		for (unsigned j = 0; j < sizeof ns / sizeof *ns; j++) {
			init();
			dump(t[k].name, ns[j], t[k].f(ns[j]));
		}
	for (unsigned j = 0; j < sizeof ns / sizeof *ns; j++) {
		init();
		dump("l_uns", ns[j], l_uns((unsigned)ns[j]));
		init();
		dump("l_long", ns[j], l_long(ns[j]));
		init();
		dump("l_ptr", ns[j], l_ptr(ns[j]));
	}
	return 0;
}
