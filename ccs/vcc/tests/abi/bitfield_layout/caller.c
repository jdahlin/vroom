#include <stdio.h>
#include "bf.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)

int main(void)
{
	struct u0 x = mk_u0(3, 4);
	struct u3 y = mk_u3(5, -300);
	struct al z;
	struct pk w = { 0 };
	struct pk2 v = mk_pk2(-(1L << 60) + 7, -200, 9);
	union un u = { 6 };

	fill_al(&z, 1, 2, -9);
	fill_pk(&w, 0x7edcba9876543210ul, 123456, 'z');
	CHECK(x.a == 3 && x.b == 4);
	CHECK(y.a == 5 && y.s == -300);
	CHECK(z.a == 1 && z.b == 2 && z.c == -9);
	CHECK(w.w == 0x7edcba9876543210ul && w.m == 123456 && w.z == 'z');
	CHECK(v.q == 1 && v.s == -(1L << 60) + 7 && v.t == -200 && v.z == 9);
	CHECK(sizes() == (int)(sizeof(struct u0) | sizeof(struct u3) << 4 | sizeof(struct al) << 8 | sizeof(struct pk) << 12 |
			       sizeof(struct pk2) << 18 | sizeof(union un) << 24 | _Alignof(struct u3) << 28));
	w.w += 5;
	v.s -= 1;
	CHECK(sum_all(x, y, z, w, v, u) == 3 + 8 + 15 - 1500 + 1 + 14 - 99 + (long)((0x7edcba9876543215ul) >> 40) + 123456 + 'z' +
	      1 + ((-(1L << 60) + 6) >> 50) - 200 + 9 + 6);
	printf("%d %d %d %d %d %d %d\n", (int)sizeof x, (int)sizeof y, (int)sizeof z, (int)sizeof w, (int)sizeof v, (int)sizeof u, bad);
	return bad;
}
