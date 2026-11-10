#include <stdio.h>
#include "rec.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)

static long cb(struct s16 x, struct s3 y, long k) { return x.a + x.b * 10 + y.a + y.b * 100 + y.c * 1000 + k * 7; }

int main(void)
{
	volatile int v = 5;
	struct s1 a = r1(v);
	struct s3 b = r3(v);
	struct s4 c = { 300, 9 };
	struct s7 d = r7(v);
	struct s8 e = { 7, 8 };
	struct s12 f = r12(v);
	struct s16 g = r16(v);
	struct s16m h = r16m(v);
	struct s24 i = r24(v);
	struct sbf j = rbf(v);

	CHECK(a.a == 5 && b.a == 5 && b.c == 7 && d.a[6] == 11);
	CHECK(f.a == 5 && f.c == 15 && g.a == 5 && g.b == -5);
	CHECK(h.a == 5 && h.b == 5L << 20 && i.a == 5 && i.c == 7);
	CHECK(j.a == 5 && j.b == 15 && j.c == -5);
	CHECK(sum_all(a, b, c, d, e, f, g, h, i, j) == 5 + 18 + 309 + (5 + 12 + 21 + 32 + 45 + 60 + 77) + 15 + 30 + 0 +
	      5 + (5L << 20) + 18 + 5 + 150 - 500);
	CHECK(spill(1, 2, 3, 4, 5, g, f, 6, e) == 1 + 4 + 9 + 16 + 25 + 30 - 35 + 40 + 90 + 150 + 66 + 84 + 104);
	CHECK(vsum(5, 0, 100L, 1, g, 2, i, 3, b, 0, -1L) == 100 + 10 + (5 + 30 + 49) + (5 + 66 + 91) - 1);
	CHECK(back(cb, 3) == 3 + 60 + 1 + 200 + 3000 + 56);
	printf("%ld %ld %d\n", sum_all(a, b, c, d, e, f, g, h, i, j), vsum(2, 1, g, 3, b), bad);
	return bad;
}
