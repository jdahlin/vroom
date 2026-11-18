/* entities every translation unit that uses them defines: one object, one
   function, whichever unit's copy the link keeps */
template <class F> int count(F f)
{
    static int n = 0;
    return ++n * 100 + f();
}
inline auto closure()
{
    return [] { return 1; };
}
inline auto local_class()
{
    struct M {
        int operator()() { return 2; }
    };
    return M{};
}
inline int bump()
{
    static int s = 0;
    return ++s;
}
inline int bump_in_lambda()
{
    auto f = [] {
        static int q = 0;
        return ++q;
    };
    return f();
}
template <class T> int templated(T t)
{
    auto l = [t] { return (int)t; };
    return count(l);
}
template <class T> T templated_local(T a)
{
    struct L {
        T v;
        T get() { return v; }
    };
    L l{a};
    return l.get();
}
