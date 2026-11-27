/* [abi:expressions]: decltype of a function parameter, and of a parameter
   pack expanded, in a requires-clause (GlyphVector::initBackendData). */
extern "C" int printf(const char *, ...);

template <class... T> struct constructible {
    static constexpr bool value = true;
};
template <class... T> constexpr bool ok = true;
template <class T> struct Strike {};

struct V {
    template <class B>
    void init(void *, auto &&...args)
        requires ok<B, Strike<B>, decltype(args)...>
    {
        printf("%zu\n", sizeof...(args));
    }
};

template <class B> int one(int, auto &&a)
    requires ok<B, decltype(a)>
{
    return a;
}

int main()
{
    V v;
    v.init<int>(nullptr, 1, 2.0f);
    int x = 0;
    v.init<int>(nullptr, x);
    printf("%d\n", one<long>(1, 5));
    return 0;
}
