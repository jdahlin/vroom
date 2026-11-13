/* kernel idioms: always_inline + constants, asm operands, DCE, varargs, cleanup, int128 */
void out(const char *name, unsigned long v);
typedef unsigned long u64; typedef unsigned int u32; typedef _Bool bool; enum { false, true };
typedef __builtin_va_list va_list;
#define __always_inline inline __attribute__((__always_inline__))
#define IS_ENABLED(x) (x)
#define BUILD_BUG_ON(c) do { extern void __bad_thing(void); if (c) __bad_thing(); } while (0)

extern void undefined_function(void);	/* must never be referenced */

static __always_inline int cap_possible(int cap)
{
	switch (cap) {
	case 1: return IS_ENABLED(1);
	case 2: return IS_ENABLED(0);
	default: return 1;
	}
}

static __always_inline bool has_cap(int cap)
{
	bool r = false;
	if (!cap_possible(cap))
		goto out;
	asm goto("b %l[yes]\n\t.pushsection .discard.x, \"a\"\n\t.word %c0\n\t.popsection" : : "i"(cap) : : yes);
	goto out;
yes:
	r = true;
out:
	return r;
}

static __always_inline int sizefn(int size, unsigned long v)
{
	BUILD_BUG_ON(!__builtin_constant_p(size));
	switch (size) {
	case 1: return (unsigned char)v;
	case 2: return (unsigned short)v;
	case 4: return (int)v;
	default: BUILD_BUG_ON(1); return 0;
	}
}

struct key { int enabled; };
struct key k1 = { 1 }, k2 = { 0 };
static __always_inline bool static_branch(struct key *const key, const bool branch)
{
	asm goto("b %l[l_yes]\n\t.pushsection .discard.jl, \"aw\"\n\t.quad %c0 - .\n\t.popsection" : : "i"(&((char *)key)[branch]) : : l_yes);
	return false;
l_yes:
	return true;
}

static __always_inline bool system_supports(void) { return IS_ENABLED(0) && has_cap(1); }

static int sum(int n, ...)
{
	va_list ap;
	int s = 0;
	__builtin_va_start(ap, n);
	for (int i = 0; i < n; i++)
		s += __builtin_va_arg(ap, int) * (i + 1);
	long l = __builtin_va_arg(ap, long);
	char *p = __builtin_va_arg(ap, char *);
	__builtin_va_end(ap);
	return s + (int)l + p[0];
}

static int vsum2(int n, va_list ap) { long s = 0; while (n--) s += __builtin_va_arg(ap, long); return (int)s; }
static int sum2(int n, ...) { va_list ap; __builtin_va_start(ap, n); int r = vsum2(n, ap); __builtin_va_end(ap); return r; }

static int cleaned;
static void cleanup_int(int *p) { cleaned += *p; }
static int with_cleanup(int x)
{
	int a __attribute__((cleanup(cleanup_int))) = x;
	for (int i = 0; i < 3; i++) {
		int b __attribute__((cleanup(cleanup_int))) = i * 10;
		if (i == 1)
			continue;
		if (x > 100)
			return b;
	}
	{
		int c __attribute__((cleanup(cleanup_int))) = 1000;
		if (x == 5)
			goto out;
	}
out:
	return a;
}

typedef unsigned __int128 u128;
static u64 mulshr(u64 a, u32 mul, unsigned int shift) { return (u64)(((u128)a * mul) >> shift); }
static u64 hi128(u64 a, u64 b) { u128 x = (u128)a * b; x += 5; x -= 1; return (u64)(x >> 64) ^ (u64)x; }
static int cmp128(u128 a, u128 b) { return (a < b) + 2 * (a == b) + 4 * (a > b); }

static int counter(void) { static int n = 10; return n++; }
static int (*fp)(int, int);
static int mulf(int a, int b) { return a * b; }
union u { u32 w; unsigned char b[4]; };
struct init { int a; const char *s; int arr[3]; union u un; } gi = { .s = "xyz", .arr = { [2] = 7 }, .un = { .b = { 1, 2, 3, 4 } } };
static const int table[] = { 10, 20, 30 };
enum color { RED = 3, GREEN, BLUE = 10 };

static int locals(int x)
{
	struct init li = { x, "ab", { 1, 2 }, { 0x01020304 } };
	char buf[] = "hello world";
	int *cl = (int[]){ x, x + 1, x + 2 };
	return li.a + li.s[1] + li.arr[1] + li.un.b[0] + buf[6] + cl[2] + (int)sizeof(buf);
}

static int bits(u64 v) { return __builtin_popcountl(v) * 1000 + __builtin_clzl(v) * 10 + __builtin_ctzl(v) + __builtin_ffs((int)v); }
static int ovf(int a, int b) { int r; long q; return __builtin_add_overflow(a, b, &r) * 2 + __builtin_mul_overflow((long)a, (long)b, &q) + (r & 0xf0); }
static int computed(int i)
{
	static void *tbl[] = { &&l0, &&l1, &&l2 };
	goto *tbl[i];
l0: return 100;
l1: return 200;
l2: return 300;
}
static u64 swap(u64 x) { return __builtin_bswap64(x) ^ __builtin_bswap32((u32)x); }
static int asm_ops(int a)
{
	int r;
	unsigned long t = 5;
	asm("add %w0, %w1, %w2" : "=r"(r) : "r"(a), "r"(3));
	asm("add %0, %0, #1" : "+r"(t));
	asm("mov %x0, %x1" : "=r"(t) : "rZ"(0UL));
	return r + (int)t;
}

void tests(void)
{
	out("has1", has_cap(1));
	out("has2", has_cap(2));
	out("has3", has_cap(3));
	out("size1", sizefn(1, 0x1ff) + sizefn(2, 0x1ffff) + sizefn(4, 7));
	out("k1", static_branch(&k1, 1));
	out("supports", system_supports());
	if (system_supports())
		undefined_function();
	if (sizeof(int) == 8)
		undefined_function();
	out("sum", sum(3, 1, 2, 3, 100L, "A"));
	out("sum9", sum(9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 50L, "B"));
	out("sum2", sum2(10, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L, 10L));
	out("cl1", with_cleanup(1)); out("cleaned", cleaned);
	out("cl5", with_cleanup(5)); out("cleaned", cleaned);
	out("cl200", with_cleanup(200)); out("cleaned", cleaned);
	out("mulshr", mulshr(0x123456789abcdefULL, 0x87654321u, 40));
	out("hi128", hi128(0xffffffffffffffffULL, 0x123456789ULL));
	out("cmp128", cmp128(5, 7) * 100 + cmp128((u128)1 << 100, 3) * 10 + cmp128(9, 9));
	out("counter", counter() + counter() * 100);
	fp = mulf;
	out("fp", fp(6, 7));
	out("gi", gi.s[2] + gi.arr[2] + gi.un.w);
	out("table", table[1] + GREEN + BLUE);
	out("locals", locals(3));
	out("bits", bits(0x00f0f0f000000100ULL));
	out("ovf", ovf(0x7fffffff, 1) * 1000 + ovf(5, 6));
	out("computed", computed(0) + computed(1) + computed(2));
	out("swap", swap(0x0102030405060708ULL));
	out("asm", asm_ops(4));
}
