/* long double constants are binary128 on aarch64 Linux and must be parsed
   and folded at that precision, not through double (TODO.md tier 1,
   "long double constants folded with strtod").  Checked by bit pattern so
   no soft-float runtime is needed. */
/* vcc-xfail: long double constants folded with strtod on Linux */
#include <stdio.h>
#include <string.h>

static const long double tenth = 0.1L, big = 1e4000L, hexf = 0x1.0000000000000000000000000001p0L;
static const int eq = 0.1L == 0.1, gt = 1e4000L > 1e308L;

static int bits(const char *what, long double x, unsigned long long hi, unsigned long long lo)
{
	unsigned long long w[2];

	memcpy(w, &x, sizeof w);
	if (w[1] == hi && w[0] == lo)
		return 0;
	printf("%s: %016llx%016llx, want %016llx%016llx\n", what, w[1], w[0], hi, lo);
	return 1;
}

int main(void)
{
	int bad = 0;

	bad |= bits("0.1L", tenth, 0x3ffb999999999999ull, 0x999999999999999aull);
	bad |= bits("1e4000L", big, 0x73e6a3750647fcabull, 0x18c21ab905450cc3ull);
	bad |= bits("0x1.0...01p0L", hexf, 0x3fff000000000000ull, 0x0000000000000001ull);
	bad |= eq != 0 || gt != 1;
	printf("%d %d\n", eq, gt);
	return bad;
}
