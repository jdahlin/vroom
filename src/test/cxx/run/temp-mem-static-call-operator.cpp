/* [over.call]/1, [temp.mem]/1: a functor whose operator() member templates are static and non-static (C++23's static operator(), clang's extension before; libc++'s __copy_impl): called as F()(x), f(x) and f.operator()(x) */
struct F {
    template <class T, class = decltype(T() + 1)> static int operator()(T t) { return 1 + t; }
    template <class T> int operator()(T t, int u) const { return 10 + u; }
};
struct G {
    template <class T> static T operator()(T t) { return t + 5; }
};
int main()
{
    F f;
    const F cf = {};
    int r = F()(1) + f(1) + cf(1) + F()(1, 2) + f(1, 2) + cf(1, 3) + G()(1) + G{}(2);
    r += f.operator()(1);
    return r - (2 + 2 + 2 + 12 + 12 + 13 + 6 + 7 + 2);
}
