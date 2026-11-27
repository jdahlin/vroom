// [over.match.class.deduct]/1.2: deduction from a specialization of the
// template itself selects the copy deduction candidate
template <class T> struct span {
    T *p;
    int n;
    span(T *q, int m) : p(q), n(m) {}
    span(const span &o) : p(o.p), n(o.n + 1) {}
    template <class U> span(const span<U> &o) : p(o.p), n(o.n + 100) {}
};
template <class T> span(T *, int) -> span<T>;
template <class T, int N> span(T (&)[N]) -> span<T>;
template <class C> span(C &&) -> span<char>;

template <class T> int count(span<T> s) { return s.n * int(sizeof(T)); }

template <class T> int f(span<T> s) { return count(span(s)); }
int g(span<long> a)
{
    span b(a);
    span c(a);
    return count(b) * 10000 + count(c) + int(sizeof(*span(a).p));
}

int main()
{
    static long v[3];
    span<long> a(v, 3);
    __builtin_printf("%d %d\n", g(a), f(a));
    return 0;
}
