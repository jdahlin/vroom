/* [expr.const]: a class copied into a member of an older object inside a
   callee keeps its elements after the call returns (fmt's FMT_STRING
   check) */
struct V {
    const char *d;
    int s;
    constexpr V(const char *p, int n) : d(p), s(n) {}
    template <class S>
    constexpr V(const S &o) : d(o.data()), s(o.size()) {}
};
struct P {
    V v;
    int id;
    constexpr explicit P(V f, int i = 0) : v(f), id(i) {}
};
struct CP : P {
    int types[1];
    constexpr CP(V f, int) : P(f), types{1} {}
};
struct Ck {
    int a[2];
    CP ctx;
    int b[2];
    constexpr Ck(V f) : a{1, 2}, ctx(f, 1), b{3, 4} {}
};
constexpr int use(V, Ck &&h)
{
    const char *lit = "wxyzwxyzwxyzwxyz";
    int more[8]{7, 7, 7, 7, 7, 7, 7, 7};
    h.ctx.v.s -= 1;
    return h.ctx.v.s * 10 + (lit[0] == 'w') + (h.ctx.v.d[0] == 'a') + more[0] - 7;
}
constexpr int f()
{
    constexpr V sv("ab", 2);
    return use(sv, Ck(sv));
}
static_assert(f() == 12);
void g()
{
    constexpr V sv("ab", 2);
    constexpr int u = (use(sv, Ck(sv)), 0);
    (void)u;
}
int main()
{
    g();
    return f() - 12;
}
