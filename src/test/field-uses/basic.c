/* select: A.n m */
/* select: A.len */
/* select: A.cb */
/* select: A.bits */
struct A { int n; int *p, len; int (*cb)(int n); unsigned bits : 3; };
struct B { int n; int len; };
int f(struct A *a, struct A v, struct B b)
{
	a->n = v.n + b.n;
	return a->len + b.len + v.bits + a->cb(1) + sizeof(a->len) + (0 ? a->n : 0);
}
