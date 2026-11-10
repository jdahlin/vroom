#include <stdio.h>
#include "al.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)
#define W(a, b) (((__int128)(b) << 64) | (unsigned long long)(a))

int main(void)
{
	struct al al = { 10, 20 };
	struct ma ma = { 10, 20 };
	tal ta = { 10, 20 };
	struct nest ne = { { 10, 20 } };
	struct pk2 pk = { W(10, 20) };
	struct ia ia = { W(10, 20) };
	struct w w = { W(10, 20) };
	long long want = 1 + 30 + 100 + 14;

	CHECK(r_al(1, al, 2) == want);
	CHECK(r_ma(1, ma, 2) == want);
	CHECK(r_tal(1, ta, 2) == want);
	CHECK(r_nest(1, ne, 2) == want);
	CHECK(r_pk2(1, pk, 2) == want);
	CHECK(r_ia(1, ia, 2) == want);
	CHECK(r_w(1, w, 2) == want);
	CHECK(r_i128(1, W(10, 20), 2, W(3, 4)) == want + 33 + 52);
	CHECK(s_al(1, 1, 1, 1, 1, 1, 1, 1, 4, al, 2) == 8 + 8 + 130 + 14);
	CHECK(s_ma(1, 1, 1, 1, 1, 1, 1, 1, 4, ma, 2) == 8 + 8 + 130 + 14);
	CHECK(s_i128(1, 1, 1, 1, 1, 1, 1, 1, 4, W(10, 20), 2) == 8 + 8 + 130 + 14);
	CHECK(v_all(5, 4, 7, 0, al, 1, ma, 2, W(1, 2), 3, w) == (((7 * 3 + 50) * 3 + 50) * 3 + 5) * 3 + 50);
	CHECK(v_all(9, 4, 1, 4, 2, 4, 3, 4, 4, 4, 5, 4, 6, 4, 7, 0, al, 1, ma) == ((((((1 * 3 + 2) * 3 + 3) * 3 + 4) * 3 + 5) * 3 + 6) * 3 + 7) * 9 + 50 * 4);
	struct hv hv = { 1, 2, 3, 4 };
	double hw = 3 + 10 + 21 + 44;

	CHECK(f_stack(1, 1, 1, 1, 1, 1, 1, 1, 4, (struct f3){ 1, 2, 3 }, 2, (struct f3){ 3, 0, 1 }) == 8 + 8 + 34 + 22 + 39 + 17);
	CHECK(h_stack(1, 1, 1, 1, 1, 1, 1, 1, 4, hv, 2) == 8 + 8 + hw + 26);
#ifndef __APPLE__	/* Apple: clang's own caller and va_arg disagree after an odd number of doubles */
	for (int k = 0; k < 10; k++) {
		double r = k == 0 ? h_va(0, hv) : k == 1 ? h_va(1, 1.0, hv) : k == 7 ? h_va(7, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, hv) :
			   k == 8 ? h_va(8, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, hv) : k == 9 ? h_va(9, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, hv) : -1;
		if (r != -1)
			CHECK(r == (k == 0 ? 0 : k) + hw);
	}
#endif
	printf("%d\n", bad);
	return bad;
}
