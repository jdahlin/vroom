/* The kernel's scoped_guard: a cleanup variable in a for's init, the
   increment jumps to a label in the body (if (0) { l: break; }).  The goto
   stays in the variable's scope: its cleanup runs once, at the break.
   vcc ran it at the goto too (x86 boot: irq descriptor unlocked twice). */
#include <stdio.h>

static int locks, unlocks;

typedef struct { int *p; } guard_t;
static void guard_exit(guard_t *g) { if (g->p) unlocks++; }
static guard_t guard_init(int *p) { locks++; return (guard_t){ p }; }

#define scoped(ptr, l) \
	for (guard_t g __attribute__((cleanup(guard_exit))) = guard_init(ptr); g.p; ({ goto l; })) \
		if (0) { \
l:			break; \
		} else

static int x;

static void f(int n)
{
	scoped(&x, lab1)
		x += n;
}

static int g2(int n)
{
	scoped(&x, lab2) {
		if (n > 5)
			return n;
		x++;
	}
	return 0;
}

int main(void)
{
	f(1);
	f(2);
	g2(1);
	g2(9);
	printf("%d %d %d\n", locks, unlocks, x);
	return 0;
}
