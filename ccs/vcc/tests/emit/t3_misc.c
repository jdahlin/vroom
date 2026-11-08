/* switch with large and ranged cases, 128-bit variable shifts, small struct
 * returns, division corners, bit-field updates, alloca, recursion */
typedef unsigned long u64;
typedef long s64;
typedef unsigned int u32;
typedef unsigned __int128 u128;
typedef __int128 s128;
void out(const char *name, u64 v);

static int sw(long r)
{
	switch (r) {
	case 0x4200: return 1;
	case 0x4201: return 2;
	case 0x4202 ... 0x4210: return 3;
	case 100000 ... 200000: return 4;
	case -5: return 5;
	case 7: return 6;
	default: return 7;
	}
}

static u64 shl128(u128 v, unsigned n) { v <<= n; return (u64)v ^ (u64)(v >> 64) * 3; }
static u64 shr128(u128 v, unsigned n) { v >>= n; return (u64)v ^ (u64)(v >> 64) * 5; }
static u64 sar128(s128 v, unsigned n) { v >>= n; return (u64)v ^ (u64)((u128)v >> 64) * 7; }

struct s12 { int a, b, c; };
struct s16 { long a, b; };
struct s3 { char a, b, c; };
static struct s12 mk12(int x) { return (struct s12){ x, x * 2, x * 3 }; }
static struct s16 mk16(long x) { struct s16 r = { x, -x }; return r; }
static struct s3 mk3(char x) { struct s3 r = { x, x + 1, x + 2 }; return r; }

static long divs(long a, long b) { return a / b * 1000 + a % b; }
static u64 divu(u64 a, u64 b) { return a / b * 1000 + a % b; }

struct bf { unsigned a : 3; int b : 5; unsigned c : 12; u64 d : 40; };
static u64 bits(int k)
{
	struct bf f = { 5, -3, 100, 0x123456789aULL };
	f.a += k; f.b -= k; f.c *= 3; f.d >>= 4; f.b |= 1;
	return (u64)f.a * 1000000 + (u64)(f.b + 16) * 10000 + f.c + f.d;
}

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static u64 dyn(int n)
{
	char *p = __builtin_alloca(n);
	u64 s = 0;

	for (int i = 0; i < n; i++)
		p[i] = (char)(i * 7);
	for (int i = 0; i < n; i++)
		s = s * 31 + (unsigned char)p[i];
	return s;
}

static int loops(int n)
{
	int s = 0, i = 0;

	do {
		if (i & 1)
			continue;
		s += i;
		if (s > 1000)
			break;
	} while (++i < n);
	return s ?: -1;
}

static s64 chars(signed char a, unsigned char b, short c) { return a * b + c - (a >> 1) + (b >> 1); }

static const struct { const char *n; int v; } tab[] = { { "one", 1 }, { "two", 2 }, { "three", 3 } };
static int lookup(const char *s)
{
	for (unsigned i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
		const char *a = tab[i].n, *b = s;

		while (*a && *a == *b)
			a++, b++;
		if (!*a && !*b)
			return tab[i].v;
	}
	return -1;
}

void tests(void)
{
	long v[] = { 0x4200, 0x4201, 0x4205, 0x4211, 150000, -5, 7, 8 };
	u64 acc = 0;

	for (int i = 0; i < 8; i++)
		acc = acc * 10 + sw(v[i]);
	out("switch", acc);
	{
		u128 x = ((u128)0x0123456789abcdefULL << 64) | 0xfedcba9876543210ULL;

		for (unsigned n = 0; n < 128; n += 13) {
			out("shl", shl128(x, n));
			out("shr", shr128(x, n));
			out("sar", sar128((s128)x | ((s128)1 << 127), n));
		}
	}
	{
		struct s12 a = mk12(7);
		struct s16 b = mk16(9);
		struct s3 c = mk3(40);

		out("s12", a.a + a.b * 100 + a.c * 10000);
		out("s16", (u64)(b.a * 1000 + b.b));
		out("s3", c.a + c.b * 256 + c.c * 65536);
	}
	out("divs", (u64)divs(-17, 5));
	out("divs2", (u64)divs(17, -5));
	out("divu", divu(0xffffffffffffffffULL, 7));
	out("bits", bits(2));
	out("fib", fib(15));
	out("dyn", dyn(37));
	out("loops", loops(100));
	out("chars", (u64)chars(-7, 200, -300));
	out("lookup", lookup("two") * 10 + lookup("three") + lookup("x") * 100);
}
