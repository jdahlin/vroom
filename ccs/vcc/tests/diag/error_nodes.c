/* no follow-on errors on an expression or declaration already diagnosed: an
   undeclared name, a missing member, a variable or field of an invalid type */
struct zz;
struct S { void a; int b; };
foo_t g;
int f(struct S *p, int *q, int k)
{
	void v;
	struct zz z;
	p->a = 1;
	q = p->a + 1;
	g->m = 3;
	q = z;
	q[undef] = 1;
	und[2] = *q;
	q = undef2->x + p->nope;
	k += *missing;
	k = k ? nothere : 0;
	k = -(&gone)[1];
	return v + q[k];
}
int h(struct zz x) { return x + 1; }
struct zz r(int a);
