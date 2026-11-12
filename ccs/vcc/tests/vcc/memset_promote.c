/* vcc-flags: -O2 */
/* locals cleared by a memset (= {0}, memset of the whole or a part) then used field by
 * field: VIR promotes them, the memset as zero stores of the fields it covers; one whose
 * field straddles the memset's end stays in memory.  An inlined callee takes the local's
 * address (i2p(p2i $slot) once folded), and a function calling setjmp is not inlined. */
#include <setjmp.h>
#include <string.h>

struct acc { long sum; int n; float f; double d; void *p; };
struct two { int a, b; };

static jmp_buf jb;

static void add(struct acc *a, int v)
{
	a->sum += v;
	a->n++;
	a->f += 0.5f;
	a->d += 1.0;
}

static __attribute__((noinline)) int pick(int v) { return v * 3; }

static void jump(void) { longjmp(jb, 1); }

static void sj(int *x)
{
	if (setjmp(jb))
		return;
	*x = 10;
	jump();
}

int main(int argc, char **argv)
{
	struct acc a = { 0 };
	struct two t;
	struct two u;
	int k = 1;

	(void)argv;
	for (int i = 0; i < 5 + argc; i++)
		add(&a, pick(i));
	if (a.sum != 3 * (0 + 1 + 2 + 3 + 4 + 5) || a.n != 6 || a.f != 3.0f || a.d != 6.0 || a.p)
		return 1;
	memset(&t, 0, sizeof t);
	if (argc > 5)
		t.b = argc;
	if (t.a || t.b)
		return 2;
	u.a = 7;
	u.b = 9;
	memset(&u, 0, 6);	/* u.b half cleared: stays in memory */
	if (u.a || u.b != (9 & ~0xffff))
		return 3;
	sj(&k);
	if (k != 10)
		return 4;
	return 0;
}
