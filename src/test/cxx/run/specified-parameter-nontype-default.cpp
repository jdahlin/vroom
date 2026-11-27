/* [temp.deduct.call]/7: a specialization with a defaulted non-type
   argument (SkSpan) naming only specified parameters */
template <class T, unsigned long E = ~0ul> struct xs {
    T *p; unsigned long n;
    template <unsigned long N> xs(T (&a)[N]) : p(a), n(N) {}
    template <class C> xs(const C &c) : p(c.p), n(c.n) {}
};
struct holder { const int *p; unsigned long n; };
template <class T> int h(xs<const T> a, xs<const T> b) { return (int)(a.n + b.n); }
extern "C" int printf(const char *, ...);
int main()
{
    const int k[3] = {1, 2, 3};
    holder x{k, 2};
    printf("%d\n", h<int>(k, x));
}
