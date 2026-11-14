#include <stddef.h>
#include "tlv.h"

static TL struct st *tl_s;
static TL long *tl_a, *tl_b;
static TL long tl_n;
static struct st pool0, *pool = &pool0;
static long cells[4];

__attribute__((noinline)) static struct st *mk(void)
{
	static struct st fresh;

	fresh.a = &cells[2], fresh.b = &cells[3], fresh.n = 7;
	return &fresh;
}

__attribute__((noinline)) static long *check(long *p) { return p; }

/* as driver/output.c's obj_state: s from a list or made, a check that leaves -1 in a temporary, then thread-locals set from s */
struct st *take(int fresh)
{
	struct st *s;

	if (tl_s)
		return tl_s;
	s = fresh ? NULL : pool;
	if (s)
		pool = s->next;
	if (!s) {
		s = mk();
		if (check(s->a) == (long *)-1)
			return NULL;
	}
	tl_s = s;
	tl_a = s->a, tl_b = s->b, tl_n = s->n;
	return s;
}

long *seen_a(void) { return tl_a; }
long *seen_b(void) { return tl_b; }
long seen_n(void) { return tl_n; }

void reset(void);
void reset(void) { tl_s = NULL, tl_a = tl_b = NULL, tl_n = 0; pool0.a = &cells[0], pool0.b = &cells[1], pool0.n = 3, pool0.next = NULL; pool = &pool0; }
