/* [temp.deduct.call], [over.ics.list]/6: lambdas among the elements of a
   braced array argument of overloaded templates (std::to_array) */
extern "C" int printf(const char *, ...);
template <class T, int N> struct array {
    T e[N];
};
template <class T, int N> constexpr array<T, N> to_array(T (&a)[N])
{
    array<T, N> r{};
    for (int i = 0; i < N; i++)
        r.e[i] = a[i];
    return r;
}
template <class T, int N> constexpr array<T, N> to_array(T (&&a)[N])
{
    array<T, N> r{};
    for (int i = 0; i < N; i++)
        r.e[i] = a[i];
    return r;
}
using Maker = int (*)(int);
struct TR {
    Maker f = nullptr;
    constexpr TR() {}
    constexpr TR(Maker p) : f(p) {}
};
static constexpr auto makers = to_array<Maker>(
    {Maker([](int x) { return x + 1; }), Maker([](int x) { return x * 2; })});
static constexpr auto tiles =
    to_array<TR>({TR([](int a) { return a - 1; }),
                  TR([](int a) { return a - 2; })});
int main()
{

    printf("%d %d %d %d\n", makers.e[0](3), makers.e[1](3),
           tiles.e[0].f(9), tiles.e[1].f(9));
}
