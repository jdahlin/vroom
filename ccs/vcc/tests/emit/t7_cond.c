/* conditions on a compare's 0/1 (cset): likely/unlikely, !, !!, == 0 / != 0,
 * widening and narrowing casts, bool results, in branches and as values */
typedef unsigned long u64;
void out(const char *name, u64 v);

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define noinline __attribute__((noinline))

static volatile int sink;

static noinline int br1(int x) { if (likely(x)) return 1; return 2; }
static noinline int br2(long *p) { if (unlikely(!p)) return 3; return 4; }
static noinline int br3(int x, int y) { if (unlikely(x < y)) return 5; return 6; }
static noinline int br4(int x, unsigned y) { if (likely(x) && unlikely(y > 3)) return 7; return 8; }
static noinline int br5(int x) { if ((x > 2) == 0) return 9; return 10; }
static noinline int br6(int x) { if ((x > 2) != 0) return 11; return 12; }
static noinline int br7(long x) { if (!!!(x & 0x100000000L)) return 13; return 14; }
static noinline int br8(signed char x) { if ((long)(x < 0)) return 15; return 16; }
static noinline int br9(unsigned x, unsigned y) { if (likely(x >= y) || !(x != 7)) return 17; return 18; }
static noinline int val1(int x) { return !(x == 0); }
static noinline _Bool val2(int x, int y) { return x < y; }
static noinline int val3(int x) { return likely(x > 5); }
static noinline long val4(int x) { return (long)!x - 1; }
static noinline int val5(int a, int b) { return (a == b) == 0; }
static noinline int val6(long a) { return !!a + !a * 2; }
static noinline unsigned char val7(int a) { return (unsigned char)(a < 0) + 1; }
static noinline long val8(int a, int b) { return -(long)(a > b); }
static noinline int val9(int x) { int r = x != 3; sink = r; return !r; }
static noinline int val10(int x) { return (x < 0 ? x > -5 : x < 5) == 0; }

void tests(void)
{
	static const long vals[] = { 0, 1, -1, 2, 3, 4, 5, 6, 7, -5, -4, 0x100000000L, -128, 127, 0x80000000L };
	u64 a = 0, b = 0, c = 0;

	for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		long x = vals[i];
		long y = vals[(i * 7 + 3) % (sizeof(vals) / sizeof(vals[0]))];

		a = a * 31 + br1((int)x) + br2(x & 1 ? 0 : (long *)&a) + br3((int)x, (int)y) + br4((int)x, (unsigned)y);
		a = a * 31 + br5((int)x) + br6((int)x) + br7(x) + br8((signed char)x) + br9((unsigned)x, (unsigned)y);
		b = b * 31 + val1((int)x) + val2((int)x, (int)y) + val3((int)x) + (u64)val4((int)x) + val5((int)x, (int)y);
		c = c * 31 + val6(x) + val7((int)x) + (u64)val8((int)x, (int)y) + val9((int)x) + val10((int)x);
	}
	out("br", a);
	out("v", b);
	out("w", c);
}
