/* vcc-xfail: arm64: an unreachable labelled statement reached only from inside it is kept (TODO.md tier 3, codegen) */
/* An unreachable statement whose label is only reached from inside it is
   dead (the kernel's scoped_cond_guard after an inlined "return 0": clang
   drops the region, and the call to a function no one defines with it).
   never_defined() would make the link fail.  A label a reachable goto
   enters stays. */
#include <stdio.h>

int never_defined(int *p);
static int locks;
static int trylock(int *p) { (void)p; locks++; return 1; }
static void unlock(int *p) { (void)p; locks--; }
typedef struct { int *l; } g_t;
static inline g_t g_init(int *p) { return (g_t){ trylock(p) ? p : 0 }; }
static inline void g_exit(g_t *g) { if (g->l) unlock(g->l); }
#define scoped_cond(p, fail, lab) \
	for (g_t s __attribute__((cleanup(g_exit))) = g_init(p); 1; ({ goto lab; })) \
		if (!s.l) { fail; lab: break; } else
static inline int supported(void) { return 0; }

static int f(int *p)
{
	if (!supported())
		return 0;
	scoped_cond(p, return 0, l1) {
		if (!never_defined(p))
			return 0;
	}
	return 1;
}

static int g(int *p, int k)
{
	if (k)
		goto in;
	if (!supported())
		return 7;
	{
in:		return *p + 1;
	}
}

int main(void)
{
	int x = 41;

	printf("%d %d %d %d\n", f(&x), g(&x, 0), g(&x, 1), locks);
	return 0;
}
