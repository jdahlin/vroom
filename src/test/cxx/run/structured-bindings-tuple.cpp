/* [dcl.struct.bind]/4: tuple-like types: std::tuple_size<E>::value,
   std::tuple_element<i, E>::type and get<i>(e) found in E's associated
   namespaces (one template, or overloads by the value category of e);
   the variables r_i bound to get's result, lvalue or rvalue references;
   e an lvalue for auto & and an xvalue otherwise; a type whose
   std::tuple_size is incomplete is bound by its members (/5) */

#define check(e)                                                               \
    do {                                                                       \
        if (!(e))                                                              \
            __builtin_abort();                                                 \
    } while (0)

typedef unsigned long size_t;

namespace std {
template <class T> struct tuple_size;
template <size_t I, class T> struct tuple_element;
template <class T> struct tuple_size<const T> {
    static constexpr size_t value = tuple_size<T>::value;
};
template <size_t I, class T> struct tuple_element<I, const T> {
    typedef const typename tuple_element<I, T>::type type;
};
} // namespace std

template <class T, class U> struct same {
    static const bool value = false;
};
template <class T> struct same<T, T> {
    static const bool value = true;
};

namespace lib {
struct Pair {
    long v[2];
    int gets;
};
/* one overload per value category of e */
template <size_t I> long &get(Pair &p)
{
    p.gets++;
    return p.v[I];
}
template <size_t I> long &&get(Pair &&p)
{
    p.gets += 10;
    return static_cast<long &&>(p.v[I]);
}

/* a value: get returns a prvalue, bound to r_i (an rvalue reference) */
struct Triple {
    int v[3];
};
template <size_t I> int get(const Triple &t)
{
    return t.v[I] * 2;
}

/* std::tuple_size is declared but not defined: /5 */
struct Plain {
    int a, b;
};
} // namespace lib

namespace std {
template <> struct tuple_size<lib::Pair> {
    static constexpr size_t value = 2;
};
template <> struct tuple_element<0, lib::Pair> {
    typedef long type;
};
template <> struct tuple_element<1, lib::Pair> {
    typedef long type;
};
template <> struct tuple_size<lib::Triple> {
    static constexpr size_t value = 3;
};
template <size_t I> struct tuple_element<I, lib::Triple> {
    typedef int type;
};
} // namespace std

lib::Pair make()
{
    lib::Pair r = {{5, 6}, 0};
    return r;
}

int main()
{
    lib::Pair p{{1, 2}, 0};
    auto &[a, b] = p;
    check(p.gets == 2);
    a = 10;
    b = 20;
    check(p.v[0] == 10 && p.v[1] == 20);
    static_assert(same<decltype(a), long>::value, "");
    static_assert(same<decltype(b), long>::value, "");

    auto [c, d] = p;
    c = 3;
    check(p.gets == 2 && p.v[0] == 10 && c == 3 && d == 20);

    auto &&[e, f] = make();
    check(e == 5 && f == 6);

    lib::Triple t{{1, 2, 3}};
    auto [x, y, z] = t;
    check(x == 2 && y == 4 && z == 6);
    static_assert(same<decltype(x), int>::value, "");

    lib::Pair ps[2] = {{{1, 2}, 0}, {{3, 4}, 0}};
    long s = 0;
    for (auto &[u, v] : ps)
        s += u * v;
    check(s == 14 && ps[0].gets == 2);

    lib::Plain q{7, 8};
    auto [qa, qb] = q;
    check(qa == 7 && qb == 8);
    return 0;
}
