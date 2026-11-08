/* many-argument calls (argument parking), error() folding through plain
 * static/inline functions (re-emission with inlining), initializer overrides,
 * __builtin_constant_p through always_inline bodies */
typedef unsigned long u64;
void out(const char *name, u64 v);

#define __always_inline inline __attribute__((always_inline))
#define BUILD_BUG_ON(c) do { extern void __bad(void) __attribute__((error("bad"))); if (c) __bad(); } while (0)

static u64 sum20(int n, ...)
{
	__builtin_va_list ap;
	u64 s = 0;

	__builtin_va_start(ap, n);
	for (int i = 0; i < n; i++)
		s = s * 3 + __builtin_va_arg(ap, u64);
	__builtin_va_end(ap);
	return s;
}

static u64 f(u64 x) { return x * 7 + 1; }

static u64 many(u64 a)
{
	return sum20(20, f(a), f(a + 1), f(a + 2), f(a + 3), f(a + 4), f(a + 5), f(a + 6), f(a + 7), f(a + 8),
		     f(a + 9), f(a + 10), f(a + 11), f(a + 12), f(a + 13), f(a + 14), f(a + 15), f(a + 16),
		     f(a + 17), f(a + 18), f(a + 19));
}

/* the kernel's fix_to_virt / patch_map shape: the check folds only once the
 * plain static caller is inlined where its argument is a constant */
static __always_inline u64 to_virt(unsigned idx)
{
	BUILD_BUG_ON(idx >= 10);
	return 0x1000 * idx;
}
static u64 map(unsigned idx) { return to_virt(idx) + 1; }

/* a plain inline function holding a BUILD_BUG_ON (io_kiocb_cmd_sz_check) */
static inline void sz_check(u64 sz) { BUILD_BUG_ON(sz > 64); }
struct cmd { char data[48]; };

static __always_inline int k(int x) { return x * 3; }
static int notconst(int x) { return x + 1; }

typedef u64 (*fn)(u64);
static u64 ident(u64 x) { return x; }
static const fn tbl[6] = { [0 ... 5] = f, [2] = 0, [4] = ident };

void tests(void)
{
	out("many", many(3));
	out("map", map(4));
	sz_check(sizeof(struct cmd));
	out("bcp", __builtin_constant_p(k(5)) * 100 + __builtin_constant_p(notconst(5)) * 10 + k(5));
	{
		u64 acc = 0;

		for (int i = 0; i < 6; i++)
			acc = acc * 10 + (tbl[i] ? tbl[i](1) : 9);
		out("tbl", acc);
	}
}
