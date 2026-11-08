/*
 * A declaration before a switch's first case is never executed.  The
 * emitter used to give it a slot at its first use, inside that statement's
 * scratch area, which a later call argument reused: ecpg's check_indicator
 * (for (p = v->members; p; p = p->next) check_indicator(p->type);) stored
 * p->type over p and crashed on ->next.
 */
#include <stdio.h>
#include <stdlib.h>

struct type;
struct member { const char *name; struct type *type; struct member *next; };
struct type { int kind; struct member *members; struct type *elem; };

static int nleaf;

static void walk(struct type *v)
{
	switch (v->kind) {
		struct member *p;
		int depth = 99;		/* initializer never runs either */

	case 1:
		nleaf++;
		break;
	case 2:
		for (p = v->members; p; p = p->next)
			walk(p->type);
		break;
	case 3:
		depth = 1;
		walk(v->elem);
		if (depth != 1)
			abort();
		break;
	}
}

/* the same in a plain block jumped into by goto */
static long sum(struct member *m)
{
	long s = 0;

	goto in;
	{
		struct member *q;
in:
		for (q = m; q; q = q->next)
			s += (long)q->name[0] + (q->type ? q->type->kind : 0);
	}
	return s;
}

int main(void)
{
	struct type i1 = { 1 }, i2 = { 1 }, i3 = { 1 }, s = { 2 }, a = { 3 };
	struct member m3 = { "c", &i3, 0 }, m2 = { "b", &i2, &m3 }, m1 = { "a", &i1, &m2 };

	s.members = &m1;
	a.elem = &s;
	walk(&a);
	printf("%d %ld\n", nleaf, sum(&m1));
	if (nleaf != 3 || sum(&m1) != 'a' + 'b' + 'c' + 3)
		abort();
	return 0;
}
