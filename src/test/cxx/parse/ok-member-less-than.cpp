// a.n < 1 || a.n > (int)4: n is a data member, so < is less-than even though
// <1 || a.n> (int)4 would parse as template arguments and a call (skcms)
struct A { unsigned n; int c[4]; template <int K> int t(int); };
bool f(const A *a) { return a->n < 1 || a->n > (int)(sizeof(a->c)); }
bool g(A a) { return a.n < 1 || a.n > (int)4; }
int h(A a) { return a.t<2>(3) + a.template t<1>(0); }
