/* A field address that several loads and stores use (x86-64 adds it again at each, so that
 * each folds it as a displacement), one that a call also takes, one used in another block
 * than where it is made, and one stored as a value. */
#include <stdio.h>

struct node {
	long pad[3];
	long count;
	struct node *next;
	long last;
};

static __attribute__((noinline)) void take(long *p, long v)
{
	*p += v;
}

static __attribute__((noinline)) long walk(struct node *n, long k)
{
	long *c = &n->count;
	long sum = 0;

	for (long i = 0; i < k; i++) {
		sum += *c;
		*c = sum + i;
		if (i & 1)
			take(c, 3);
	}
	n->last = *c;
	return sum + n->last;
}

static __attribute__((noinline)) long split(struct node *n, int sel)
{
	long *c = &n->count;
	long r = *c;

	if (sel > 0)
		*c = r * 2;
	else
		*c = r + 7;
	return *c + r;
}

static __attribute__((noinline)) long *escape(struct node *n, long **out)
{
	long *c = &n->count;

	*out = c;
	*c += 5;
	return c;
}

int main(void)
{
	struct node a = { {0}, 10, 0, 0 }, b = { {0}, 3, 0, 0 };
	long *p = 0, *q;
	long r1 = walk(&a, 6), r2 = split(&b, 1), r3 = split(&b, -1);

	q = escape(&b, &p);
	printf("%ld %ld %ld %ld %ld %d %ld\n", r1, r2, r3, a.count, a.last, p == q, b.count);
	return 0;
}
