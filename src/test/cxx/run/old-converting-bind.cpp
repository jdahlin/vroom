// a const reference to a class bound to another type: a temporary made by the converting constructor
// (a string literal for a const std::string &, an int for a class from int); destroyed by the end of the
// block (vcc destroys temporaries at the block's end, not the full expression's: PLAN-cxx.md)
static int live, made;
struct S { const char *p; unsigned n; S(const char *s) : p(s), n(0) { while (s[n]) n++; live++; made++; } S(const S &o) : p(o.p), n(o.n) { live++; } ~S() { live--; } };
struct N { int v; N(int x) : v(x * 2) {} };
static unsigned len(const S &s) { return s.n + (unsigned)(s.p[0] == 'k') * 100; }
static int twice(const N &n) { return n.v; }
int main()
{
	unsigned a;
	int b;

	{
		a = len("kappa");
		b = twice(21);
	}
	return a == 105 && b == 42 && made == 1 && live == 0 ? 0 : 1;
}
