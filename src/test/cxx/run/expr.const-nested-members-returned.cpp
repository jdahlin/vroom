/* [expr.const]: a returned or remade class object whose members are classes
   several levels deep keeps its subobjects past the constructor's
   invocation */
struct L {
    const int &v;
    constexpr L(const int &x) : v(x) {}
};
struct M {
    L l;
    constexpr M(const int &x) : l(x) {}
};
struct T {
    M m;
    constexpr T(const int &x) : m(x) {}
};
constexpr T tie(const int &a)
{
    return T(a);
}
constexpr int f(int a)
{
    T t = tie(a);
    T u = t;
    return t.m.l.v * 10 + u.m.l.v;
}
static_assert(f(2) == 22);
struct Out {
    static constexpr int value = f(3);
};

int main()
{
    static constexpr int r = f(4);
    if (Out::value != 33 || r != 44)
        return 1;
    return 0;
}
