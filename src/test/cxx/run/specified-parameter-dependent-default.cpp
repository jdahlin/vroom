/* [temp.deduct.call]/7: a defaulted argument dependent on a specified
   parameter takes no part in deduction */
template <class T> constexpr bool reloc = true;
template <class T, bool M = reloc<T>> struct tarr { int v; };
template <class T> struct span {
    T *p; int n;
    template <int N> span(T (&a)[N]) : p(a), n(N) {}
};
template <class T> int g(span<tarr<T> *> a) { return a.n; }
extern "C" int printf(const char *, ...);
int main()
{
    tarr<int> t;
    tarr<int> *arrays[] = {&t, &t};
    printf("%d\n", g<int>(arrays));
}
