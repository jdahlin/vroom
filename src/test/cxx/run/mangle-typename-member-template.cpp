template<class P> struct Ops { template<class I> using dt = long; };
template<class P, class I, class O>
O cn(I first, typename Ops<P>::template dt<I> n, O r) { return r; }
struct X;
int g(){ int a[3] = {5, 6, 7}; return *cn<X>(a, 2, a); }
int main() { if (g() != 5) return 1; return 0; }
