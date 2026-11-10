#include <stdio.h>
#include "fp.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)

static double cb(struct d2 x, float f, long double l) { return x.a + x.b * 10 + f * 100 + (double)l * 1000; }

int main(void)
{
	volatile double v = 2.0;
	struct f2 f2 = rf2((float)v);
	struct f3 f3 = rf3((float)v);
	struct d2 d2 = rd2(v);
	struct di di = rdi(v, 5);
	struct id id = rid(3);
	struct fi3 fi3 = rfi3((float)v);
	struct d3 d3 = rd3(v);

	CHECK(rf(1.5f, v) == 3.0f);
	CHECK(rd(3, v, 0.25f) == 3.5);
	CHECK(rld(1.5L, 4, 0.25L) == 5.75L);
	CHECK(f2.a == 2 && f2.b == -2 && f3.c == 6 && d2.b == 0.5 && di.d == 2 && di.i == 5);
	CHECK(id.i == 3 && id.f == 3.5f && id.d == 3.75 && fi3.f == 2 && fi3.i == 6 && fi3.c == 'x' && d3.c == 4);
	CHECK(many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, d2, f3, 12) ==
	      1 + 4 + 9 + 16 + 25 + 36 + 49 + 64 + 81 + 100 + 121 + 24 + 6.5 + 28 + 60 + 96 + 204);
	CHECK(mixed(di, id, fi3, f2, 0.5L, d3) == 2 + 10 + 9 + 14 + 18.75 + 12 + 42 + 'x' + 16 - 18 + 5 + 2 + 33 + 52);
	CHECK(vsum(6, 0, 1.5, 1, 4, 2, d2, 3, di, 4, 2.0L, 5, f3) == 1.5 + 12 + 2 + 2.5 + 14 + 5 + 22 + 2 + 8 + 18);
	CHECK(vsum(10, 0, 1.0, 0, 2.0, 0, 3.0, 0, 4.0, 0, 5.0, 0, 6.0, 0, 7.0, 0, 8.0, 0, 9.0, 2, d2) == 45 + 2 + 2.5);
	CHECK(back(cb, 2) == 2 + 40 + 100 + 6000);
	printf("%g %g %d\n", mixed(di, id, fi3, f2, 0.5L, d3), vsum(2, 0, 1.25, 4, 3.0L), bad);
	return bad;
}
