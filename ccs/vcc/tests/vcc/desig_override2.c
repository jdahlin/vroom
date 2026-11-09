/* More designated-initializer overrides (TODO.md tier 1): parts of a struct
   value with arrays inside, wide and signed strings, ranges, and a union
   value read again through the member a designator picks.  Output compared
   with clang build. */
/* vcc-flags: -Wno-initializer-overrides */
#include <stdio.h>
#include <stddef.h>
struct in { int x, y; };
struct arr { short a[3]; struct in n; char tail; };
union u { int i; char c[4]; };
struct hu { union u u; int z; };
static int calls;
static int side(void) { return ++calls; }
int main(void)
{
	struct arr av = { { 1, 2, 3 }, { 4, 5 }, 'q' };
	struct { struct arr r; int k; } w = { .r = av, .r.a[1] = 70, .r.n.x = 40 };
	wchar_t ws[5] = { [0] = 0 };
	struct { wchar_t s[5]; } wq = { .s = L"xyz", .s[1] = L'Q' };
	signed char sc[4] = { 0 }; struct { signed char s[4]; } sq = { .s = "\xff\x80", .s[2] = 1 };
	int r[6] = { [0 ... 3] = 7, [1] = 50, [0 ... 5] = 8 };
	int r2[6] = { [0 ... 3] = 7, [1] = 50 };
	union u uv = { .i = 0x01020304 };
	struct hu h = { .u = uv, .u.c[0] = 9 };
	(void)ws; (void)sc;
	printf("w %d %d %d %d %d %c %d\n", w.r.a[0], w.r.a[1], w.r.a[2], w.r.n.x, w.r.n.y, w.r.tail, w.k);
	printf("wq %d %d %d %d\n", (int)wq.s[0], (int)wq.s[1], (int)wq.s[2], (int)wq.s[3]);
	printf("sq %d %d %d %d\n", sq.s[0], sq.s[1], sq.s[2], sq.s[3]);
	printf("r %d %d %d %d %d %d\n", r[0], r[1], r[2], r[3], r[4], r[5]);
	printf("r2 %d %d %d %d %d %d calls %d\n", r2[0], r2[1], r2[2], r2[3], r2[4], r2[5], calls);
	printf("h %d %d %d %d %d\n", h.u.c[0], h.u.c[1], h.u.c[2], h.u.c[3], h.z);
	return 0;
}
