/* [expr.type.conv]/1-2 with [dcl.type.simple]/1: a qualified type-name in
   functional notation, with a braced-init-list or parentheses: a prvalue of
   the class; libc++'s customization point objects (__concepts/swappable.h:
   inline constexpr auto swap = __swap::__fn{}) */
namespace ns {
namespace impl {
struct fn {
    void operator()(int &a, int &b) const
    {
        int t = a;
        a = b;
        b = t;
    }
};
struct add {
    int operator()(int a, int b) const { return a + b; }
};
typedef int integer;
} // namespace impl
inline namespace cpo {
inline constexpr auto swap = impl::fn{};
}
} // namespace ns
int main()
{
    int a = 1, b = 40;
    ns::swap(a, b);
    auto f = ns::impl::add{};
    ns::impl::add g = ns::impl::add();
    int c = ns::impl::integer{2};
    return f(a, b) + g(c, 0) - 43;
}
