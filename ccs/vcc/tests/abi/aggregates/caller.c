#include <stdio.h>
#include <string.h>
#include "agg.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)

long long caller_cb(struct s12 a, long double b, struct s1 c)
{
	return a.a * 1000 + (long long)(a.b * 10) + a.c + (long long)(b * 4) + c.c;
}

static double my_va(int n, ...)
{
	va_list ap;
	double r;

	va_start(ap, n);
	r = f_vlist(n, ap);	/* a va_list made here, read there */
	va_end(ap);
	return r;
}

int main(void)
{
	struct s1 a1 = f_s1((struct s1){ 'a' });
	struct s2 a2 = f_s2((struct s2){ 1234 });
	struct sf af = f_sf((struct sf){ 1.25f }, 4);
	struct sd ad = f_sd((struct sd){ 10 });
	struct nest an = f_nest((struct nest){ { { 2 } } });
	struct arr1 aa = f_arr1((struct arr1){ { 0x1234 } });
	struct s3 a3 = f_s3((struct s3){ 1, 2, 3 });
	struct s8 a8 = f_s8((struct s8){ 5, 7 });
	struct s12 ab = f_s12((struct s12){ 1, 2.5f, 3 });
	struct s16 ac = f_s16((struct s16){ 1LL << 40, 3 });
	struct mix am = f_mix((struct mix){ 'q', 2.5, -7 });
	struct big big = { { 1, 2, 3, 4, 5, 1LL << 50 } };
	struct big ag = f_big(big, 3);
	struct bits bt = f_bits((struct bits){ 5, 300, 1000001 });
	union un u = { .c = { 9, 0, 0, 0, 0, 0 } };
	union un au = f_un(u);
	struct w128 w = f_w128((struct w128){ (__int128)1 << 100 });
	__int128 q = f_i128(-((__int128)1 << 80), 3, (unsigned __int128)0xabcdef << 64);
	long double ld = f_ld(1e300L, 2.0, 0.5L);
	_Complex float cf = f_cf(1.0f + 2.0if);
	_Complex double cd = f_cd(1.0 + 1.0i, 2.0 - 3.0i);
	_Complex long double cl = f_cld(3.0L + 4.0iL);
	struct s12 ms = { 3, 0.5f, 4 };
	int z = 11;

	CHECK(a1.c == 'b');
	CHECK(a2.s == (short)(1234 * -3));
	CHECK(af.f == 5.0f);
	CHECK(ad.d == 3.5);
	CHECK(an.a.in.f == 2.5f);
	CHECK(aa.a[0] == (0x1234 ^ 0x5a5a));
	CHECK(a3.a == 3 && a3.b == 2 && a3.c == 1);
	CHECK(a8.a == 7 && a8.b == 12);
	CHECK(ab.a == 2 && ab.b == 5.0f && ab.c == 2);
	CHECK(ac.a == 3 && ac.b == (1LL << 40) - 3);
	CHECK(am.c == ('q' ^ 1) && am.d == -2.5 && am.s == 93);
	CHECK(ag.v[0] == 3 && ag.v[3] == 15 && ag.v[5] == 3 * (1LL << 50) + 5);
	CHECK(big.v[0] == 1);	/* the callee's copy, not ours */
	CHECK(bt.a == 2 && bt.b == 303 && bt.c == 500000);
	CHECK(au.c[5] == 10 && au.c[0] == 10);
	CHECK(w.v == ((__int128)3 << 100) - 1);
	CHECK(q == -((__int128)3 << 80) + 0xabcdef);
	CHECK(ld == 2e300L - 0.5L);
	CHECK(__real__ cf == 2.0f && __imag__ cf == 5.0f);
	CHECK(__real__ cd == 0.0 && __imag__ cd == 4.0);
	CHECK(__real__ cl == -4.0L && __imag__ cl == 4.0L);
	CHECK(f_e0((struct e0){}, 4, (struct e0){}, 5) == 405);
	CHECK(f_many(1, 2, 0.5f, 0.25, 3, -1, 0.5L, 2, ms, big) == 1 + 4 + 2 + 2 + 48 - 32 + 32 + 256 + 3 + 4 + 0 + 1 + (1LL << 50));
	CHECK(f_va(4, 'i', 5, 'l', 1LL << 40, 'd', 0.5, 'p', &z) == 5 + (double)(1LL << 40) + 0.5 + 11);
	CHECK(f_va(4, 'L', 2.5L, 'q', (__int128)1 << 66, 's', ms, 'b', big) == 2.5 + Q((__int128)1 << 66) + 7.5 + 15 + (double)(1LL << 50));
	CHECK(my_va(2, 'd', 1.25, 's', ms) == 1.25 + 7.5);
	CHECK(f_cb(caller_cb, 7) == 7000 + 15 - 7 + 63 + 'x');
	CHECK(call_back_va(my_va) == (long long)(-4 + 1.5 + 6 + 21 + Q((__int128)1 << 60) + 7));
	CHECK(call_back_va(f_va) == call_back_va(my_va));
	printf("%d %d %g %d %lld %d\n", a1.c, a2.s, (double)af.f, ab.a, ag.v[5], bad);
	return bad;
}
