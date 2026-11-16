// Q<L> with L a value parameter in a template's signature: mangled X T_ E (clang's _ZN1QILi0EEC2IiLi1EEET_RKS_IXT0_EE)
template <int K> struct Q { int v; Q(int v) : v(v) {} template <class U, int L> Q(U, const Q<L> &o) : v(o.v + 1) {} };
template <class U, int N> int f(U u, Q<N> q) { return q.v + u; }
int main() { Q<1> a(3); Q<0> b(1, a); return b.v + f(1, a) == 8 ? 0 : 1; }
