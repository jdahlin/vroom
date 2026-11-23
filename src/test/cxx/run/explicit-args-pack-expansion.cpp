/* [temp.arg.explicit]/9, [temp.variadic]/5: a pack expansion among the
   explicit template arguments of a function template call, in a class
   template's member (analysed again with the pack bound) */
namespace N {
template <class... T>
constexpr int count()
{
    return sizeof...(T);
}
template <class... T, bool F = false>
constexpr auto count_auto()
{
    return sizeof...(T);
}
template <class A, class... T>
constexpr int lead()
{
    return 100 + sizeof...(T);
}
template <int... Ns>
constexpr int sum()
{
    return (0 + ... + Ns);
}
template <class... T>
constexpr int forwarded(T... t)
{
    return sizeof...(T);
}
} // namespace N

template <class... Ts>
struct S {
    using type = decltype(N::count<Ts...>());
    using ptrs = decltype(N::count<Ts *...>());
    static constexpr int a = N::count<Ts...>();
    static constexpr int b = N::count_auto<Ts *...>();
    static constexpr int c = N::lead<int, Ts...>();
    static constexpr int d = N::count<int, Ts...>();
};

template <int... Ns>
struct V {
    static constexpr int s = N::sum<Ns...>();
};

template <class... Ts>
constexpr int at_scope()
{
    return N::count<Ts...>() + N::count<char, Ts...>();
}

static_assert(S<>::a == 0 && S<>::b == 0 && S<>::c == 100 && S<>::d == 1);
static_assert(S<int, long, char>::a == 3 && S<int, long, char>::b == 3);
static_assert(S<int, long>::c == 102 && S<int, long>::d == 3);
static_assert(V<1, 2, 3>::s == 6 && V<>::s == 0);
static_assert(at_scope<int, int>() == 5);

int main()
{
    return S<int, char>::a + V<4, 5>::s == 11 ? 0 : 1;
}
