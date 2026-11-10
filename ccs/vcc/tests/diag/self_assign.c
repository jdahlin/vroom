/* diag-flags: -Wall */
int g; struct S { int f; } s;
#define SELF(x) x = x
void f(int p) {
	int m = 0;
	m = m;
	p = p;
	g = g;
	s.f = s.f;
	(m) = (m);
	m = (m);
	SELF(m);
	m += m;
	volatile int v = 0; v = v;
	int *q = &m; *q = *q;
	typedef int T1;
	typedef int T2; T2 u = 0; (void)u;
	{ typedef long T3; }
	struct L { int a; };
	typedef struct L LT;
}
typedef int GT;
