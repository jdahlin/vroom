/* Two compares joined by && or || into one condition (VIR at -O2: the builder's
   vw_condmerge, lowered to cmp + ccmp/ccmn; by default where the result is a value,
   -fvir-ccmp=all also for plain branches).  Every condition code on both sides of and
   and or, 32 and 64 bits, signed and unsigned, immediates ccmp takes (0..31), ccmn
   takes (-1..-31) and neither (32, -32, large), a constant on the left, chains of
   three, and/or mixed, and a select whose arms are computed first.  Each form is
   checked against the same condition evaluated one compare at a time. */
#define NI __attribute__((noinline))
typedef long long S;
typedef unsigned long long U;

static volatile int vz;	/* (reads of it keep the reference's compares apart) */
static int ref2(int p, int q, int isor) { int a = p + vz, b = q + vz; return isor ? a | b : a & b; }

#define AND2(n, T, o1, o2) NI int n##_a(T a, T b, T c, T d) { return a o1 b && c o2 d; }
#define OR2(n, T, o1, o2)  NI int n##_o(T a, T b, T c, T d) { return a o1 b || c o2 d; }
#define BR2(n, T, o1, o2)  NI int n##_b(T a, T b, T c, T d) { if (a o1 b && c o2 d) return vz + 7; if (a o2 b || c o1 d) return vz + 3; return 0; }
#define ALL(n, T, o1, o2) AND2(n, T, o1, o2) OR2(n, T, o1, o2) BR2(n, T, o1, o2)

ALL(ee, int, ==, ==) ALL(en, int, ==, !=) ALL(ne, int, !=, ==) ALL(lg, int, <, >) ALL(gl, int, >, <)
ALL(le, int, <=, >=) ALL(ge, int, >=, <=) ALL(lq, int, <, ==) ALL(qg, int, ==, >=)
ALL(ulg, unsigned, <, >) ALL(ugl, unsigned, >, <) ALL(ule, unsigned, <=, >=) ALL(uge, unsigned, >=, <=)
ALL(xlg, S, <, >) ALL(xle, S, <=, >=) ALL(xee, S, ==, !=)
ALL(ylg, U, <, >) ALL(yle, U, <=, >=)

NI int k1(int a, int b) { return a == 5 && b == 31; }
NI int k2(int a, int b) { return a == 5 && b == 32; }
NI int k3(int a, int b) { return a == 5 && b == -1; }
NI int k4(int a, int b) { return a == 5 && b == -31; }
NI int k5(int a, int b) { return a == 5 && b == -32; }
NI int k6(int a, int b) { return a == 5 || b > 100000; }
NI int k7(S a, S b) { return a > 5 || b < -31; }
NI int k8(S a, S b) { return a < 0x123456789ll && b >= -7; }
NI int k9(unsigned a, unsigned b) { return a > 3 && b < 0xfffffff0u; }
NI int k10(int a, int b) { return 9 < a && 17 >= b; }	/* constants on the left */
NI int k11(U a, U b) { return a != 1 || b <= 30; }

NI int c3a(int a, int b, int c) { return a < b && b < c && c < 100; }
NI int c3o(int a, int b, int c) { return a < b || b < c || c < 100; }
NI int mx1(int a, int b, int c) { return (a == 1 || b == 2) && c == 3; }
NI int mx2(int a, int b, int c) { return (a == 1 && b == 2) || c == 3; }
NI int mx3(int a, int b, int c) { return a == 1 || (b == 2 && c == 3); }
NI int sel(int a, int b, int x, int y) { return (a < b && x > y) ? x + 1 : y - 1; }
NI S sel2(S a, S b, S x, S y) { return (a < b || x == y) ? x : y; }
NI int bit(int a, int b) { return (a == 1) & (b == 2); }
NI int bor(int a, int b) { if ((a < 5) | (b > 7)) return vz + 1; return vz; }
NI int neg(int a, int b) { return !(a == 1 && b == 2); }

static const int iv[] = { -0x7fffffff - 1, -33, -32, -31, -2, -1, 0, 1, 2, 3, 5, 9, 10, 17, 18, 30, 31, 32, 33, 99, 100, 101, 100001, 0x7fffffff };
#define NV ((int)(sizeof iv / sizeof iv[0]))

int main(void)
{
	/* the macro families on a grid of 6^4 values */
	static const int g[] = { -0x7fffffff - 1, -1, 0, 1, 2, 0x7fffffff };

	for (int i = 0; i < 6; i++) for (int j = 0; j < 6; j++) for (int k = 0; k < 6; k++) for (int l = 0; l < 6; l++) {
		int a = g[i], b = g[j], c = g[k], d = g[l];
		unsigned ua = (unsigned)a, ub = (unsigned)b, uc = (unsigned)c, ud = (unsigned)d;
		S xa = (S)a << 20, xb = (S)b << 20, xc = (S)c << 20, xd = (S)d << 20;
		U ya = (U)xa, yb = (U)xb, yc = (U)xc, yd = (U)xd;

#define CHK(n, A, B, C, D, o1, o2, code) do { \
		int p = A o1 B, q = C o2 D, p2 = A o2 B, q2 = C o1 D; \
		if (n##_a(A, B, C, D) != ref2(p, q, 0)) return code; \
		if (n##_o(A, B, C, D) != ref2(p, q, 1)) return code + 1; \
		if (n##_b(A, B, C, D) != (ref2(p, q, 0) ? 7 : ref2(p2, q2, 1) ? 3 : 0)) return code + 2; } while (0)
		CHK(ee, a, b, c, d, ==, ==, 10); CHK(en, a, b, c, d, ==, !=, 13); CHK(ne, a, b, c, d, !=, ==, 16);
		CHK(lg, a, b, c, d, <, >, 19); CHK(gl, a, b, c, d, >, <, 22); CHK(le, a, b, c, d, <=, >=, 25);
		CHK(ge, a, b, c, d, >=, <=, 28); CHK(lq, a, b, c, d, <, ==, 31); CHK(qg, a, b, c, d, ==, >=, 34);
		CHK(ulg, ua, ub, uc, ud, <, >, 37); CHK(ugl, ua, ub, uc, ud, >, <, 40);
		CHK(ule, ua, ub, uc, ud, <=, >=, 43); CHK(uge, ua, ub, uc, ud, >=, <=, 46);
		CHK(xlg, xa, xb, xc, xd, <, >, 49); CHK(xle, xa, xb, xc, xd, <=, >=, 52); CHK(xee, xa, xb, xc, xd, ==, !=, 55);
		CHK(ylg, ya, yb, yc, yd, <, >, 58); CHK(yle, ya, yb, yc, yd, <=, >=, 61);
	}
	for (int i = 0; i < NV; i++) for (int j = 0; j < NV; j++) {
		int a = iv[i], b = iv[j];
		S xa = (S)a * 0x10001, xb = (S)b * 3;

		if (k1(a, b) != ref2(a == 5, b == 31, 0)) return 70;
		if (k2(a, b) != ref2(a == 5, b == 32, 0)) return 71;
		if (k3(a, b) != ref2(a == 5, b == -1, 0)) return 72;
		if (k4(a, b) != ref2(a == 5, b == -31, 0)) return 73;
		if (k5(a, b) != ref2(a == 5, b == -32, 0)) return 74;
		if (k6(a, b) != ref2(a == 5, b > 100000, 1)) return 75;
		if (k7(xa, xb) != ref2(xa > 5, xb < -31, 1)) return 76;
		if (k8(xa, xb) != ref2(xa < 0x123456789ll, xb >= -7, 0)) return 77;
		if (k9((unsigned)a, (unsigned)b) != ref2((unsigned)a > 3, (unsigned)b < 0xfffffff0u, 0)) return 78;
		if (k10(a, b) != ref2(9 < a, 17 >= b, 0)) return 79;
		if (k11((U)xa, (U)xb) != ref2((U)xa != 1, (U)xb <= 30, 1)) return 80;
		if (bit(a, b) != ref2(a == 1, b == 2, 0)) return 81;
		if (bor(a, b) != ref2(a < 5, b > 7, 1)) return 82;
		if (neg(a, b) != !ref2(a == 1, b == 2, 0)) return 83;
		for (int k = 0; k < NV; k++) {
			int c = iv[k];

			if (c3a(a, b, c) != ref2(ref2(a < b, b < c, 0), c < 100, 0)) return 84;
			if (c3o(a, b, c) != ref2(ref2(a < b, b < c, 1), c < 100, 1)) return 85;
			if (mx1(a, b, c) != ref2(ref2(a == 1, b == 2, 1), c == 3, 0)) return 86;
			if (mx2(a, b, c) != ref2(ref2(a == 1, b == 2, 0), c == 3, 1)) return 87;
			if (mx3(a, b, c) != ref2(a == 1, ref2(b == 2, c == 3, 0), 1)) return 88;
			if (sel(a, b, c, 9) != (ref2(a < b, c > 9, 0) ? c + 1 : 8)) return 89;
			if (sel2(xa, xb, c, 9) != (ref2(xa < xb, c == 9, 1) ? c : 9)) return 90;
		}
	}
	return 0;
}
