void out(const char *name, unsigned long v);
typedef unsigned long u64; typedef long s64; typedef unsigned int u32; typedef unsigned char u8; typedef signed char s8;
typedef unsigned short u16; typedef short s16;

int g = 7, garr[10] = { 1, 2, 3, [7] = 9 };
static int sg;
const char *msg = "hello";
struct pt { int x, y; long z; } gp = { 1, -2, 3 };
struct bf { unsigned a : 3, b : 5; int c : 7; unsigned long d : 40; } gbf = { 5, 17, -9, 0x123456789aUL };

static int add(int a, int b) { return a + b; }
static long many(long a, long b, long c, long d, long e, long f, long g_, long h, long i, long j) { return a - b + c * d - e + f + g_ * h + i - j; }
static struct pt mkpt(int x) { struct pt p = { x, x * 2, x * 3L }; return p; }
struct big { long v[5]; };
static struct big mkbig(long x) { struct big b; for (int i = 0; i < 5; i++) b.v[i] = x + i; return b; }
static long sumbig(struct big b) { long s = 0; for (int i = 0; i < 5; i++) s += b.v[i]; return s; }
static long sumpt(struct pt p) { return p.x + p.y + p.z; }
static int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
static int sw(int x) {
	switch (x) {
	case 0: return 10;
	case 1: case 2: return 20;
	case 5 ... 9: return 30;
	case -3: return 40;
	default: return 50;
	}
}
static u32 shifts(u32 a, int s) { return (a << s) ^ (a >> (s + 1)) ^ (u32)((int)a >> 3); }
static s64 divs(s64 a, s64 b) { return a / b + a % b; }
static u64 udivs(u64 a, u64 b) { return a / b * 1000 + a % b; }
static int cmp(long a, unsigned long b) { return (a < 0) + 2 * (b > 5) + 4 * (a == (long)b) + 8 * (a >= -3); }
static int loops(int n) {
	int s = 0, i = 0;
	while (i < n) { if (i == 3) { i++; continue; } s += i; i++; }
	do { s *= 2; } while (s < 1000);
	for (int k = 0; k < 10; k++) { if (k == 7) break; s -= k; }
	return s;
}
static int gotos(int n) {
	int r = 0;
again:
	r += n;
	if (--n > 0) goto again;
	return r;
}
static u8 narrow(u32 x) { u8 c = (u8)x; s8 d = (s8)x; u16 h = (u16)x; s16 i = (s16)x; return (u8)(c + d + h + i); }
static long ext(int x, unsigned y, s8 z, u16 w) { return (long)x + (long)y + z + w; }
static int ternary(int a, int b) { return a > b ? a : b > 10 ? 10 : b; }
static int logic(int a, int b) { int r = (a && b) + 2 * (a || b) + 4 * !a; if (a && !b) r += 100; return r; }
static void bfops(struct bf *p) { p->a += 3; p->b = p->c; p->c = -1; p->d >>= 4; }
static int ptrs(int *p, int n) { int *e = p + n; int s = 0; while (p < e) s += *p++; return s + (int)(e - p); }
static int incs(void) { int a = 5; int b = a++ + ++a; int c = a-- - --a; return a * 100 + b * 10 + c; }
static int compound(int x) { x += 3; x *= 4; x -= 1; x /= 3; x %= 7; x <<= 2; x >>= 1; x |= 0x40; x &= 0x7f; x ^= 5; return x; }
static int stmtexpr(int x) { return ({ int t = x * 2; t + 1; }) + ({ x; }); }
static u64 mul64(u64 a, u64 b) { return a * b; }
static int glob(void) { sg += g; garr[3] = sg; return garr[3] + garr[7] + gp.y + msg[1]; }

void tests(void)
{
	struct bf b = gbf;
	struct pt p = mkpt(4);
	int arr[] = { 3, 4, 5, 6 };

	out("add", add(3, 4));
	out("many", many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
	out("mkpt", p.x + p.y * 10 + p.z * 100);
	out("big", sumbig(mkbig(10)));
	out("sumpt", sumpt(gp));
	out("fact", fact(10));
	for (int i = -4; i < 12; i++) out("sw", sw(i));
	out("shifts", shifts(0x80001234u, 5));
	out("divs", divs(-17, 5));
	out("udivs", udivs(123456789, 1000));
	out("cmp", cmp(-1, 6) * 16 + cmp(6, 6));
	out("loops", loops(8));
	out("gotos", gotos(5));
	out("narrow", narrow(0x1234f789));
	out("ext", ext(-5, 0xffffffffu, -3, 0xffff));
	out("tern", ternary(3, 20) * 100 + ternary(5, 2));
	out("logic", logic(1, 0) * 100 + logic(0, 1));
	bfops(&b);
	out("bf", b.a | b.b << 3 | (u64)(u32)b.c << 8);
	out("bfd", b.d);
	out("ptrs", ptrs(arr, 4));
	out("incs", incs());
	out("compound", compound(10));
	out("stmtexpr", stmtexpr(7));
	out("mul64", mul64(0x123456789ULL, 0xfedcbaULL));
	out("glob", glob());
	out("glob2", glob());
}
