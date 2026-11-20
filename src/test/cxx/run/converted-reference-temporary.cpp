// a reference to a class bound to another type: the temporary made by a converting constructor
// ([dcl.init.ref]/5.4), destroyed in reverse order of construction; an incomplete class
// bound by a reference to the same class
extern "C" int printf(const char *, ...);
struct S {
	int v;
	S(int x) : v(x) { printf("S(%d)\n", x); }
	S(const S &o) : v(o.v) { printf("copy %d\n", o.v); }
	~S() { printf("~S(%d)\n", v); }
};
struct N { const char *p; N(const char *s) : p(s) {} };
struct Inc;
struct E { int k; E(const S &s) : k(s.v + 1) {} E(const S &, const N &n) : k(n.p[0]) {} };
static const Inc &same(const Inc &x) { return x; }
static const Inc &again(const Inc &x) { return same(x); }
static int first(const N &n) { return n.p[0]; }
static int sum(const S &a, S &&b) { return a.v + b.v; }
void taken(const Inc &x) { again(x); }
int main()
{
	E e(5);
	printf("%d %d\n", e.k, first("xyz"));
	printf("%d\n", sum(1, 2));
	const S &r = 7;
	S &&rr = 8;
	printf("%d %d\n", r.v, rr.v);
	E e2(3, "q");
	printf("%d\n", e2.k);
	return 0;
}
