#include <stdio.h>
#include "vec.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)
#define EQ4(v, a, b, c, d) ((v)[0] == (a) && (v)[1] == (b) && (v)[2] == (c) && (v)[3] == (d))

static v4si cb(v4si a, v8qi q) { return a * (int)q[7] + (int)q[0]; }

int main(void)
{
	v4si a = { 1, 2, 3, 4 }, b = { 5, 6, 7, 8 };
	v4si r = add4(a, b);
	v8qi q = { 10, 20, 30, 40, 50, 60, 70, 80 };
	v4hi h = { 1, 2, 3, 4 };
	v8qi m = mix8(3, q, 5.0, h);
	struct hfa2 s = { 0.5f, 0.25f };
	v4sf f = fma4((v4sf){ 1, 2, 3, 4 }, (v4sf){ 2, 2, 2, 2 }, (v4sf){ 0, 1, 0, 1 }, s);
	v2df one = { 1, -1 };
	v2df mn = many(one, one, one, one, one, one, one, one, one, 7);
	struct hva2 h2 = swap2((struct hva2){ a, b });
	struct hva3 h3 = rot3((struct hva3){ q, q + 1, q + 2 });
	struct hva4 x = { { { 1, 2 }, { 3, 4 } }, { 5, 6 }, { 7, 8 } };
	struct hva4 y = sum4(x, x);
	v8si big = { 1, 2, 3, 4, 5, 6, 7, 8 };
	struct mix mx = mixed((struct mix){ a, 1.5 }, big);
	v8si b3 = big8(big);

	CHECK(EQ4(r, 51, 62, 73, 84));
	CHECK(m[0] == 13 && m[1] == 25 && m[2] == 34 && m[7] == 80);
	CHECK(f[0] == 2.25f && f[1] == 5.25f && f[2] == 6.25f && f[3] == 9.25f);
	CHECK(mn[0] == 45 + 7 && mn[1] == -45 + 7);
	CHECK(EQ4(h2.a, 5, 6, 7, 8) && EQ4(h2.b, 1, 2, 3, 4));
	CHECK(h3.a[0] == 12 && h3.b[0] == 10 && h3.c[0] == 11 && h3.c[7] == 81);
	CHECK(y.a[0][0] == 2 && y.a[1][1] == 8 && y.b[1] == 12 && y.c[0] == 0);
	CHECK(EQ4(mx.a, 2, 4, 6, 12) && mx.d == 3.0);
	CHECK(b3[0] == 3 && b3[7] == 24);
	CHECK(small4((v4qi){ 1, 2, 3, 4 }, (v4qi){ 5, 6, 7, 8 }) == 1000 + 400 + 60 + 7);
	CHECK(vsum(3, a, b, (v4si){ 9, 9, 9, 9 }) == 4321 + 8765 + 9999);
	CHECK(EQ4(call_back(cb, a), 10, 18, 26, 34));
	printf("%d %d %g %d %d\n", r[3], m[0], (double)f[3], h3.c[7], bad);
	return bad;
}
