/* [abi:mangle.template-param] in a requires-clause of a member template: the
   class template's parameters (T_, T0_), the member's own (TL0__, TL0_0_),
   a variable of the scope as L_Z <encoding> E. */
typedef unsigned long size_t;
constexpr size_t dynamic = (size_t)-1;
template <class T, class C> concept compatible = sizeof(C) > 0;
template <class C> constexpr size_t computed = dynamic;

template <class T, size_t Extent = dynamic> struct span {
    T *p;
    size_t n;
    constexpr span(T *q, size_t m) : p(q), n(m) {}
    template <typename Container, size_t N = computed<Container>>
        requires(compatible<T, Container> &&
                 (Extent == dynamic || N == dynamic || Extent == N))
    constexpr explicit(Extent != dynamic && N == dynamic)
        span(Container &&c) : span(c.p, c.n) {}
};
struct view { int *p; size_t n; };

int main()
{
    view v{0, 0};
    span<int> s(v);
    return (int)s.n;
}
