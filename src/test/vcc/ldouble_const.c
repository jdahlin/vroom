/* long double constants are binary128 on aarch64 Linux and must be parsed
   and folded at that precision, not through double (TODO.md tier 1,
   "long double constants folded with strtod").  Checked by bit pattern so
   no soft-float runtime is needed; bench/vcc-ldconst.py checks thousands
   more against clang. */
#include <stdio.h>
#include <string.h>

static const long double tenth = 0.1L, big = 1e4000L, hexf = 0x1.0000000000000000000000000001p0L;
static const int eq = 0.1L == 0.1, gt = 1e4000L > 1e308L, tiny = 1e-4950L != 0, sub = (0.1L - 0.1) != 0;
static const long double vals[] = {
	__LDBL_MAX__, __LDBL_MIN__, __LDBL_DENORM_MIN__, 1e-4950L, 1e4933L,
	0x1.00000000000000000000000000008p0L, 0x1.00000000000000000000000000018p0L,	/* ties to even */
	0.1L * 3, 1.0L / 3, 1e-30L + 1.0L, -0x1p-16494L, (long double)-170141183460469231731687303715884105727.0L,
	(long double)0xffffffffffffffffull, 0.1L - 0.1,
};
static const unsigned long long want[][2] = {
	{ 0x7ffeffffffffffffull, 0xffffffffffffffffull }, { 0x0001000000000000ull, 0x0000000000000000ull },
	{ 0x0000000000000000ull, 0x0000000000000001ull }, { 0x0000000000000000ull, 0x00057c9647e1a018ull },
	{ 0x7fff000000000000ull, 0x0000000000000000ull }, { 0x3fff000000000000ull, 0x0000000000000000ull },
	{ 0x3fff000000000000ull, 0x0000000000000002ull }, { 0x3ffd333333333333ull, 0x3333333333333334ull },
	{ 0x3ffd555555555555ull, 0x5555555555555555ull }, { 0x3fff000000000000ull, 0x0000000000001448ull },
	{ 0x8000000000000000ull, 0x0000000000000001ull }, { 0xc07e000000000000ull, 0x0000000000000000ull },
	{ 0x403effffffffffffull, 0xfffe000000000000ull }, { 0xbfc5999999999999ull, 0x9980000000000000ull },
};
static const long long sat[] = { (long long)1e30L, (long long)-1e30L, (int)-5e9L, (unsigned char)300.0L };

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
	for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
		char w[16];

		snprintf(w, sizeof w, "vals[%u]", i);
		bad |= bits(w, vals[i], want[i][0], want[i][1]);
	}
	{
		long double x = 0.1L;	/* a constant in code, not data */

		bad |= bits("x", x, 0x3ffb999999999999ull, 0x999999999999999aull);
	}
	bad |= eq != 0 || gt != 1 || tiny != 1 || sub != 1;
	bad |= sat[0] != 0x7fffffffffffffffll || sat[1] != -0x7fffffffffffffffll - 1 || sat[2] != -0x7fffffff - 1 || sat[3] != 255;
	printf("%d %d %d %d\n", eq, gt, tiny, sub);
	return bad;
}
