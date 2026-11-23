/* [dcl.struct.bind]: structured bindings of arrays (/3: e a copy, its
   elements copied one by one, or a reference) and of classes (/5: the
   direct non-static data members, or all of one base's; reference
   members, bit-fields, an unnamed bit-field left out); auto, auto &,
   const auto & (a temporary's lifetime extended) and auto &&; in a
   range-based for statement; decltype of a binding, the referenced type
   ([dcl.type.decltype]/1.1) */

#define check(e)                                                               \
    do {                                                                       \
        if (!(e))                                                              \
            __builtin_abort();                                                 \
    } while (0)

template <class T, class U> struct same {
    static const bool value = false;
};
template <class T> struct same<T, T> {
    static const bool value = true;
};

struct P {
    int x;
    long y;
};
struct R {
    int &r;
    const char c;
};
struct B {
    int u, v;
};
struct D : B {};
struct Bits {
    unsigned a : 3;
    unsigned : 2;
    unsigned b : 5;
};
struct Counted {
    int n;
    Counted(int n) : n(n) {}
    Counted(const Counted &o) : n(o.n + 100) {}
};

P make()
{
    return P{7, 8};
}

int sum(P (&ps)[3])
{
    int s = 0;
    for (auto [x, y] : ps)
        s += x + (int)y;
    for (auto &[x, y] : ps)
        x = 1;
    for (const auto &[x, y] : ps)
        s += x;
    return s;
}

int main()
{
    P p{1, 2};
    auto [a, b] = p;
    a = 10;
    check(p.x == 1 && a == 10 && b == 2);
    auto &[c, d] = p;
    c = 5;
    check(p.x == 5);
    const auto &[e, f] = make();
    check(e == 7 && f == 8);
    auto &&[g, h] = make();
    g++;
    check(g == 8 && h == 8);
    auto [pa, pb]{p};
    auto [qa, qb](p);
    check(pa == 5 && qb == 2);

    int arr[2] = {3, 4};
    auto [i, j] = arr;
    i = 30;
    check(arr[0] == 3 && i == 30 && j == 4);
    auto &[k, l] = arr;
    l = 40;
    check(arr[1] == 40);
    Counted cs[2] = {Counted(1), Counted(2)};
    auto [c0, c1] = cs;
    check(c0.n == 101 && c1.n == 102);
    auto &[r0, r1] = cs;
    check(r0.n == 1 && &r1 == &cs[1]);

    int n = 3;
    R r{n, 'z'};
    auto [rr, rc] = r;
    rr = 9;
    check(n == 9 && rc == 'z');

    D dd;
    dd.u = 1;
    dd.v = 2;
    auto [du, dv] = dd;
    check(du + dv == 3);

    Bits bits{5, 17};
    auto [ba, bb] = bits;
    check(ba == 5 && bb == 17);
    bb = 3;
    check(bb == 3 && bits.b == 17);

    P ps[3] = {{1, 2}, {3, 4}, {5, 6}};
    check(sum(ps) == 24);
    check(ps[2].x == 1);

    /* [dcl.type.decltype]/1.1: the referenced type */
    static_assert(same<decltype(a), int>::value, "");
    static_assert(same<decltype(b), long>::value, "");
    static_assert(same<decltype(e), const int>::value, "");
    static_assert(same<decltype(rr), int &>::value, "");
    static_assert(same<decltype(rc), const char>::value, "");
    static_assert(same<decltype(i), int>::value, "");
    static_assert(same<decltype((a)), int &>::value, "");
    const auto [ca, cb] = p;
    static_assert(same<decltype(ca), const int>::value, "");
    static_assert(same<decltype(cb), const long>::value, "");
    decltype(rr) alias = n;
    alias = 11;
    check(n == 11);
    return 0;
}
