/* ms_abi functions called on x86-64 Linux, sysv_abi ones on Windows (msabi.h): the
   convention that is not the target's own, clang and vcc on either side */
#include <stdio.h>
#include "msabi.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)

static OTHER int cb(int a, double b, struct s8 s) { return a * 1000 + (int)(b * 10) + s.a + s.b * 100; }

int main(void)
{
	CHECK(ints(1, 2, 3, 4LL << 32, 5, 6, 7) == 1 + 6 + 15 + 4 + 35 + 6 + 77);
	CHECK(fps(1, 2, 3, 4, 5, 6) == 1 + 4 + 9 + 16 + 25 + 36);
	CHECK(mixed(1, 2, 3, 4, 5, 6.5) == 1 + 20 + 300 + 4000 + 50000 + 6.5);
	CHECK(r1((struct s1){ 5 }, (struct s1){ 7 }).c == 12);
	CHECK(r2((struct s2){ -11 }).s == -33);
	struct s3 x3 = r3((struct s3){ { 1, 2, 3 } }, 10);
	CHECK(x3.c[0] == 11 && x3.c[1] == 2 && x3.c[2] == -7);
	CHECK(r4((struct s4){ 1.5f }, 4).f == 6.0f);
	struct s8 x8 = r8((struct s8){ 1, 2 }, (struct s8){ 30, 40 });
	CHECK(x8.a == 41 && x8.b == -28);
	struct s12 x12 = r12((struct s12){ 1, 2, 3 }, 5);
	CHECK(x12.a == 6 && x12.b == 10 && x12.c == -2);
	struct s16 x16 = r16((struct s16){ 1.5, 100 }, (struct s16){ 2.25, 1 });
	CHECK(x16.d == 3.75 && x16.l == 99);
	struct s24 x24 = r24(3, (struct s24){ 1, 2, 3 });
	CHECK(x24.a == 4 && x24.b == 8 && x24.c == 12);
	struct sd2 xd = rsd2((struct sd2){ 1.25, -2.5 });
	CHECK(xd.x == -2.5 && xd.y == 1.25);
	struct empty_pad p = { 1, 0.5 }, xp = rpad(p, p, p, p, p);
	CHECK(xp.c == 5 && xp.d == 2.5);
	CHECK(ld(1.5L, 4, 0.25L) == 6.25L);
	CHECK(i128((__int128)1 << 70, 5, 3) == ((__int128)3 << 70) + 5);
	_Complex double z = cplx(1.0 + 2.0i, 0.5f - 1.0fi);
	CHECK(__real__ z == 2.5 && __imag__ z == 3.0);
	CHECK(many(1, 2, 3, 4, 5, 6.5, 7, 8.25, 9, (struct s12){ 1, 2, 3 }) ==
	      1 + 4 + 9 + 16 + 25 + 39 + 49 + 66 + 81 + 1 + 20 + 300);
	CHECK(callback(cb, 7) == 7000 + 35 + 7 + 800 + 1);
	{
		/* values live across the call in this side's callee-saved registers */
		double a = bad + 1.25, b = a * 3, c = b - 1;
		long long u = bad + 100, v = u * 7;
		int k = keep(3);

		CHECK(k == 135 && a == 1.25 && b == 3.75 && c == 2.75 && u == 100 && v == 700);
	}
#ifndef _WIN32
	CHECK(ms_va(4, 1, 2.5, 3, 4.25) == 10.75);
#endif
	printf("msabi %d\n", bad);
	return bad;
}
