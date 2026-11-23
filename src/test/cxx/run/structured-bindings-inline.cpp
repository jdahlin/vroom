/* [dcl.struct.bind]/4 with libc++'s layout: std::tuple_size and
   std::tuple_element in an inline namespace of std ([namespace.qual]),
   get<i> in an inline namespace of E's namespace ([basic.lookup.argdep]/2);
   decltype of a binding and of a member access through one
   ([dcl.type.decltype]/1); a const object whose destructor runs is
   writable ([class.dtor]/5) */

#define check(e)                                                               \
    do {                                                                       \
        if (!(e))                                                              \
            __builtin_abort();                                                 \
    } while (0)

typedef unsigned long size_t;

namespace std {
inline namespace __1 {
template <class T> struct tuple_size;
template <size_t I, class T> struct tuple_element;
} // namespace __1
} // namespace std

template <class T, class U> struct same {
    static const bool value = false;
};
template <class T> struct same<T, T> {
    static const bool value = true;
};

namespace lib {
struct T {
    int a, b;
};
inline namespace v1 {
template <size_t I> int &get(T &t)
{
    if (I == 0)
        return t.b;
    return t.a;
}
} // namespace v1
struct P {
    long x;
};
} // namespace lib

namespace std {
inline namespace __1 {
template <> struct tuple_size<lib::T> {
    static constexpr size_t value = 2;
};
template <size_t I> struct tuple_element<I, lib::T> {
    typedef int type;
};
} // namespace __1
} // namespace std

struct Dt {
    int v;
    ~Dt() { v = 0; }
};
const Dt dt = {9};

struct Q {
    lib::P p;
    char c;
};

int main()
{
    lib::T t{1, 2};
    auto &[x, y] = t;
    /* get<0> is b: bound by get, not by the members */
    check(x == 2 && y == 1);
    x = 5;
    check(t.b == 5);
    check((same<decltype(x), int>::value));
    Q q{{7}, 'c'};
    auto [p, c] = q;
    check((same<decltype(p), lib::P>::value));
    check((same<decltype(p.x), long>::value));
    check((same<decltype(c), char>::value));
    check(p.x == 7 && c == 'c');
    check(dt.v == 9);
    return 0;
}
