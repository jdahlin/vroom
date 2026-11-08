/* locals only ever assigned constants: their value follows forward edges
 * (and is dropped at loop heads), so references behind them fold away like
 * at clang -O2.  missing() does not exist: a surviving call fails the link. */
typedef unsigned long u64;
void out(const char *name, u64 v);
extern int missing(void);

#define __always_inline inline __attribute__((always_inline))

/* the kernel's alternative_has_cap_unlikely(), with the cap impossible */
static __always_inline int possible(int cap) { return cap < 0; }
#define has_cap(cap) ({							\
	__label__ l_yes, l_out;						\
	int ret = 0;							\
	if (!possible(cap))						\
		goto l_out;						\
	asm goto("b %l[l_yes]" :::: l_yes);				\
	goto l_out;							\
l_yes:									\
	ret = 1;							\
l_out:									\
	ret;								\
})
static inline int supports_x(void) { return has_cap(5); }

/* map_kernel(): set to false on some paths, never true */
static int scs(volatile int *p)
{
	int en = 0;

	if (*p)
		en = 0;
	if (*p > 1) {
		en = 0;
		*p = 3;
	}
	if (en)
		return missing();
	return *p;
}

/* a loop changes the value: no folding inside or after it */
static int loop(volatile int *p)
{
	int done = 0, n = 0;

	while (!done) {
		n++;
		if (n == *p)
			done = 1;
	}
	return n * 10 + done;
}

/* both arms agree / disagree */
static int join(volatile int *p)
{
	int a, b;

	if (*p) {
		a = 2;
		b = 3;
	} else {
		a = 2;
		b = 4;
	}
	if (a != 2)
		return missing();
	return a * 100 + b;
}

/* goto backwards: the label is a loop head */
static int back(volatile int *p)
{
	int st = 0, k = 0;
again:
	k++;
	if (st == 0 && k < *p) {
		st = 0;
		goto again;
	}
	st = 1;
	if (k < 7)
		goto again;
	return k * 10 + st;
}

/* switch: every case agrees, the default does not */
static int sw(volatile int *p)
{
	int r = 1;

	switch (*p) {
	case 1:
		r = 5;
		break;
	case 2:
		r = 5;
		break;
	default:
		r = 6;
		break;
	}
	return r;
}

/* the kernel's vruntime_cmp(): the operator picked by name */
#define cmp(A, OP, B) ({ int r_ = 0; if (!__builtin_strcmp(OP, "<")) r_ = (A) < (B); \
	else if (!__builtin_strcmp(OP, ">")) r_ = (A) > (B); else r_ = missing(); r_; })

/* lockdep_assert_rcu_helper(): an argument the body never reads */
extern const struct tok *TOK;
static __always_inline int helper(int c, const struct tok *t) { return c + 1; }

/* shrinker_alloc(): a va_list (a 32-byte struct) handed to a stub that ignores it */
static inline int name_alloc(int a, __builtin_va_list ap) { return a; }
static int vfmt(int a, ...)
{
	__builtin_va_list ap;
	int r;

	__builtin_va_start(ap, a);
	r = name_alloc(a, ap) + __builtin_va_arg(ap, int);
	__builtin_va_end(ap);
	return r;
}

void tests(void)
{
	volatile int v = 2, w = 4, z = 0;

	if (supports_x())
		out("x", missing());
	out("scs", (u64)scs(&v));
	out("loop", (u64)loop(&w));
	out("join", (u64)join(&z));
	z = 1;
	out("join1", (u64)join(&z));
	out("back", (u64)back(&w));
	z = 2;
	out("sw2", (u64)sw(&z));
	z = 9;
	out("sw9", (u64)sw(&z));
	out("cmp", (u64)(cmp(v, "<", w) * 10 + cmp(v, ">", w)));
	out("helper", (u64)helper(v, TOK));
	out("vfmt", (u64)vfmt(3, 4));
}
