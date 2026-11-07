/* __cleanup: the kernel's guard() and scoped_guard() with every way out of
 * a scope (return, break, continue, goto, falling off, statement expressions);
 * lock/unlock calls are logged, so a cleanup run twice or skipped shows */
typedef unsigned long u64;
void out(const char *name, u64 v);

static u64 log_, depth;
static void lock(int k) { log_ = log_ * 16 + k; depth++; }
static void unlock(int k) { log_ = log_ * 16 + 8 + k; depth--; }

typedef struct { int k; } guard_t;
static inline guard_t guard_ctor(int k) { lock(k); return (guard_t){ k }; }
static inline void guard_dtor(guard_t *g) { if (g->k) unlock(g->k); }

#define guard(k) guard_t __attribute__((cleanup(guard_dtor))) __UNIQ(g) = guard_ctor(k)
#define __UNIQ(p) __CAT(p, __COUNTER__)
#define __CAT(a, b) __CAT2(a, b)
#define __CAT2(a, b) a##b
static inline void *guard_ptr(guard_t *g) { return (void *)(long)g->k; }
/* cleanup.h's scoped_guard: a for loop whose step jumps to a label in its body */
#define __scoped_guard(k, label)						\
	for (guard_t __attribute__((cleanup(guard_dtor))) scope = guard_ctor(k);	\
	     guard_ptr(&scope) || 0;						\
	     ({ goto label; }))							\
		if (0) {							\
label:										\
			break;							\
		} else
#define scoped_guard(k) __scoped_guard(k, __UNIQ(sg))

static int f_return(volatile int *p)
{
	guard(1);
	if (*p)
		return 5;
	return 6;
}

static int f_scoped(volatile int *p)
{
	int r = 0;

	scoped_guard(2) {
		if (*p == 1)
			return 7;
		r = 1;
	}
	scoped_guard(3)
		r += 2;
	return r;
}

static int f_loop(volatile int *p)
{
	int n = 0;

	for (int i = 0; i < 4; i++) {
		guard(4);
		if (i == *p)
			continue;
		if (i == 3)
			break;
		n++;
	}
	return n;
}

static int f_goto(volatile int *p)
{
	int n = 0;
again:
	{
		guard(5);
		n++;
		if (n < *p)
			goto again;
		if (n == 9)
			goto out;
	}
	n += 10;
out:
	return n;
}

static int f_nested(volatile int *p)
{
	guard(1);
	scoped_guard(2) {
		guard(3);
		if (*p)
			return 1;
	}
	return 2;
}

static int f_stmtexpr(volatile int *p)
{
	int v = ({ guard(6); *p + 1; });

	return v + ({ guard(7); scoped_guard(2) { if (*p) break; } 3; });
}

static int f_switch(volatile int *p)
{
	switch (*p) {
	case 0: {
		guard(1);
		return 1;
	}
	case 1: {
		guard(2);
		break;
	}
	default:
		scoped_guard(3)
			return 3;
	}
	return 4;
}

#define T(f, ...) do { int r_; log_ = 0; depth = 0; r_ = f(__VA_ARGS__); out(#f, (u64)r_); out("  log", log_); out("  depth", depth); } while (0)

void tests(void)
{
	volatile int z = 0, o = 1, t = 2, th = 3;

	T(f_return, &z); T(f_return, &o);
	T(f_scoped, &z); T(f_scoped, &o);
	T(f_loop, &o); T(f_loop, &th);
	T(f_goto, &th); T(f_goto, &z);
	T(f_nested, &z); T(f_nested, &o);
	T(f_stmtexpr, &z); T(f_stmtexpr, &o);
	T(f_switch, &z); T(f_switch, &o); T(f_switch, &t);
}
