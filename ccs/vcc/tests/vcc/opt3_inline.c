/* vcc-flags: -O3 */
/*
 * -O3 inlining heuristics (call.c opt_inline): small functions, static
 * functions called once, constant arguments, recursion, and what must not
 * be inlined or must keep its semantics when it is.  clang returns a local
 * with a cleanup through the return slot (NRVO: the cleanup sees the
 * returned object), so a function doing that stays a call.
 */
#include <stdio.h>
#include <setjmp.h>

typedef struct { long double a; } tstld;
typedef struct { int a, b, c, d; } quad;

void cleanup_ld(tstld *p) { p->a = 90.0; }
void cleanup_q(quad *p) { p->a = 7; }

tstld ret_cleanup_ld(void)
{
	tstld __attribute__((cleanup(cleanup_ld))) n;

	n.a = 42.0;
	return n;
}

quad ret_cleanup_q(void)
{
	quad __attribute__((cleanup(cleanup_q))) n = { 1, 2, 3, 4 };

	return n;
}

int add(int a, int b) { return a + b; }
static int twice(int x) { return add(x, x); }

static int once(int n)
{
	int s = 0;

	for (int i = 0; i < n; i++)
		s += i * i;
	if (n > 5)
		s -= add(n, 1);
	return s;
}

static int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

static int sel(int k, int a)
{
	switch (k) {
	case 0: return a;
	case 1: return a + 1;
	case 2: return a * 3;
	default: return -a;
	}
}

static jmp_buf jb;
static int jumps(int x)
{
	if (setjmp(jb))
		return x + 100;
	return x;
}

int main(void)
{
	tstld t = ret_cleanup_ld();
	quad q = ret_cleanup_q();
	volatile int v = 9;
	int r = 0;

	printf("%Lf %d %d\n", t.a, q.a, q.d);
	printf("%d %d %d\n", add(2, 3), twice(v), once(v));
	printf("%d %d\n", fact(v), fact(5));
	for (int i = 0; i < 4; i++)
		r += sel(i, v) + sel(1, i);
	printf("%d %d\n", r, jumps(v));
	return 0;
}
