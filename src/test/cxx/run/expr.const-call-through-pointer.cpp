/* [expr.call]/1 in a constant expression: a call through a pointer to
   function (fmt's parse_funcs_) */
constexpr void inc(int &c)
{
    ++c;
}
template <class T>
constexpr int size_of(int add)
{
    return sizeof(T) + add;
}
struct table {
    void (*funcs[2])(int &);
    int (*sizes[2])(int);
    constexpr table()
        : funcs{&inc, inc}
        , sizes{&size_of<char>, size_of<int>}
    {
    }
};
constexpr int f()
{
    table t;
    int n = 0;
    t.funcs[0](n);
    auto p = t.funcs[1];
    p(n);
    (*p)(n);
    return n * 100 + t.sizes[0](1) * 10 + t.sizes[1](0);
}
static_assert(f() == 324);
constexpr int u = (f(), 0);
int main()
{
    return f() - 324 + u;
}
