/* [temp.deduct.call]/7: a parameter whose template-parameters are all
   explicitly specified takes implicit conversions, a nested specialization
   among its arguments too */
extern "C" int printf(const char *, ...);
template <class T> struct span {
    T *p; int n;
    template <int N> span(T (&a)[N]) : p(a), n(N) {}
    span(T *a, int m) : p(a), n(m) {}
};
template <class T> struct tarr { int v; };
template <class T> int f(span<const T> a, span<const T> b) { return a.n + b.n; }
template <class T> int g(int x, span<tarr<T> *> a, span<const int> s) { return x + a.n + s.n; }
int main()
{
    const int k[3] = {1, 2, 3};
    int m[2] = {4, 5};
    tarr<int> t;
    tarr<int> *arrays[] = {&t, &t};
    printf("%d %d\n", f<int>(k, k), g<int>(1, arrays, k));
}
