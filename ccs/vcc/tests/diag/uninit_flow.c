/* D8.5: what is not a read of an uninitialized variable, and joins inside a region
   that may be skipped (each a false positive once, on the kernel) */
/* diag-flags: -Wall -Wno-unused */
struct res { long start, end; int flags; };
struct node { struct node *next; int v; };
int g(int);
int ok_complit(long a, long b)	/* a compound literal's values, not its layout */
{
	struct node *p;
	struct res r;
	int arr[4];

	r = (struct res){ .start = a, .end = b };
	(void)(int [4]){ [1] = 2, [2 ... 3] = 1 };
	for (p = 0; p; p = p->next)
		if (g(p->v))
			return 1;
	return (int)r.start + arr[0];
}
int ok_void(struct node *h)	/* (void)x */
{
	struct node *it;

	for ((void)it, it = h; it; it = 0)
		g(it->v);
	return 0;
}
int ok_guarded(int a, int b)	/* the join inside '&&' does not make the use certain */
{
	int t, has = 1;

	if (a)
		t = 1;
	else if (b)
		t = 2;
	else
		has = 0;
	if (has && (!b || g(t)))
		return t;
	return 0;
}
int bad_void(void)	/* (void)(x + 1) reads x */
{
	int x;

	(void)(x + 1);
	return 0;
}
