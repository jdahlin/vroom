// [abi:expressions] in a dependent return type: a subscript (ix), a
// functional cast to a class named by a qualified name (cv), and an
// enumerator, unqualified or qualified, as its value (L <type> <value> E).
// The names must be clang's.
namespace R
{
struct NoOp {
};
enum Tags { kDraw = 1, kMulti = 4 };
enum Signed { kBelow = -3 };
} // namespace R

template <bool B, class T> struct enable {
};
template <class T> struct enable<true, T> {
    using type = T;
};
template <bool B, class T> using enable_t = typename enable<B, T>::type;

template <typename Fn, typename... A>
auto map(Fn fn, const A &...args) -> decltype(fn(args[0]...))
{
    return fn(args[0]...);
}
template <typename F> auto visit(F &&f) -> decltype(f(R::NoOp()))
{
    return f(R::NoOp());
}
template <typename F> auto braced(F &&f) -> decltype(f(::R::NoOp{}))
{
    return f(R::NoOp{});
}

namespace R
{
struct S {
    template <typename T>
    enable_t<!(T::k & kDraw) || (T::k & kMulti), int> operator()(T *)
    {
        return 1;
    }
    template <typename T> enable_t<(T::k & R::kDraw) != 0, int> g(T *)
    {
        return 2;
    }
    template <typename T> enable_t<(T::k != kBelow), int> h(T *)
    {
        return 3;
    }
};
} // namespace R

struct K {
    static constexpr int k = 5;
};
static int twice(int x)
{
    return 2 * x;
}
static int ten(R::NoOp)
{
    return 10;
}

int main()
{
    int v[2] = {21, 2};
    R::S s;
    K *k = nullptr;
    if (map(twice, v) != 42)
        return 1;
    if (visit(ten) != 10 || braced(ten) != 10)
        return 2;
    if (s(k) != 1 || s.g(k) != 2 || s.h(k) != 3)
        return 3;
    return 0;
}
