/* [over.match.class.deduct]/1.2: the copy deduction candidate for a
   prvalue of the template's own specialization */
extern "C" int printf(const char *, ...);
template <class T> struct span {
    T *p;
    int n;
    span(T *a, int m) : p(a), n(m) {}
    template <int N> span(T (&a)[N]) : p(a), n(N) {}
};
template <class T> span<T> make(T *p, int n) { return span<T>(p, n); }
int main()
{
    int a[5] = {1, 2, 3, 4, 5};
    span x = span(a + 1, 3);
    span y = make(a, 2);
    printf("%d %d %d %d\n", x.p[0], x.n, y.p[1], y.n);
}
