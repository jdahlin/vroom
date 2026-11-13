/*
 * common/f128.h's x87 extended arithmetic (the ...p functions at Q_P80) against
 * the host's x87 long double, bit for bit: + - * /, comparison, integer and
 * double conversions, decimal and hex parsing, over random values (normal,
 * subnormal, infinite, NaN, short significands).  x86-64 hosts only:
 *   bench/vcc-f80.sh [N]     (N rounds, default 200000; 2000000 ~25 s)
 */
#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include "../../include/common/f128.h"
typedef unsigned __int128 U;
/* host x87 long double -> held layout */
static U of_ld(long double x) {
	unsigned char b[16] = {0}; memcpy(b, &x, 10);
	uint64_t m; memcpy(&m, b, 8); unsigned se = b[8] | b[9] << 8;
	unsigned e = se & 0x7fff, s = se >> 15;
	return (U)s << 127 | (U)e << 112 | (U)(m & ((1ull << 63) - 1)) << 49;
}
static long double to_ld(U q) { unsigned char b[16] = {0}; q_to_x87(q, b); long double x; memcpy(&x, b, 10); memset((char*)&x + 10, 0, 6); return x; }
static uint64_t st = 88172645463325252ull;
static uint64_t rnd(void) { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return st; }
static long double rld(void) {
	unsigned char b[16] = {0}; uint64_t m = rnd(); unsigned e, k = rnd() % 8;
	if (k == 0) e = 0; else if (k == 1) e = 0x7fff; else if (k < 4) e = 16383 + (int)(rnd() % 80) - 40; else e = rnd() % 0x7fff;
	if (e) m |= 1ull << 63; else m &= ~(1ull << 63);
	if (rnd() % 4 == 0) m &= ~0ull << (rnd() % 64);	/* short significands: exact cases */
	unsigned se = (unsigned)(rnd() & 1) << 15 | e; memcpy(b, &m, 8); b[8] = se; b[9] = se >> 8;
	long double x; memcpy(&x, b, 10); return x;
}
static int same(long double a, long double b) {
	if (a != a && b != b) return 1;	/* any NaN */
	return !memcmp(&a, &b, 10);
}
int main(int argc, char **argv) {
	long bad = 0, n = 0, rounds = argc > 1 ? atol(argv[1]) : 200000;
	for (long i = 0; i < rounds; i++) {
		long double a = rld(), b = rld(); U qa = of_ld(a), qb = of_ld(b);
		long double r[4] = { a + b, a - b, a * b, a / b };
		U q[4] = { q_addp(qa, qb, Q_P80), q_subp(qa, qb, Q_P80), q_mulp(qa, qb, Q_P80), q_divp(qa, qb, Q_P80) };
		for (int k = 0; k < 4; k++, n++) if (!same(r[k], to_ld(q[k]))) { if (bad++ < 8) printf("op%d %La %La: host %La vcc %La\n", k, a, b, r[k], to_ld(q[k])); }
		if (q_cmp(qa, qb) != (a != a || b != b ? 2 : a < b ? -1 : a > b ? 1 : 0)) { if (bad++ < 8) printf("cmp %La %La\n", a, b); }
		long long v = (long long)rnd() >> (rnd() % 64); unsigned long long u = rnd();
		n += 3;
		if (!same((long double)v, to_ld(q_of_ip(v, 1, Q_P80)))) { if (bad++ < 8) printf("i2f %lld\n", v); }
		if (!same((long double)u, to_ld(q_of_ip((__int128)u, 0, Q_P80)))) { if (bad++ < 8) printf("u2f %llu\n", u); }
		if (a == a && a > -9e18L && a < 9e18L && (long long)a != (long long)q_to_int(qa, 64, 1)) { if (bad++ < 8) printf("f2i %La\n", a); }
		double d = (double)a;
		n++; if (memcmp(&d, &(double){q_to_d(qa)}, 8) && d == d) { if (bad++ < 8) printf("f2d %La\n", a); }
	}
	/* parsing */
	for (long i = 0; i < rounds / 6; i++) {
		char buf[96]; long double a = rld(); if (a != a || a - a != 0) continue;
		int prec = (int)(rnd() % 40) + 1;
		if (rnd() & 1) snprintf(buf, sizeof buf, "%.*Le", prec, a); else snprintf(buf, sizeof buf, "%La", a);
		long double h = strtold(buf, NULL); U q = q_parsep(buf, strlen(buf), Q_P80);
		n++; if (!same(h, to_ld(q))) { if (bad++ < 16) printf("parse %s: host %La vcc %La\n", buf, h, to_ld(q)); }
	}
	printf("%ld checks, %ld wrong\n", n, bad);
	return bad != 0;
}
