/* a member template's constraint naming the class template's parameter in a
   type (libc++'s convertible_to<_Tp, _Range&> in ref_view's constructor):
   the parameter, not its argument, apart from the member's own at the same
   position; a pack expansion of the member's own pack beside it
   (__movable_box's is_constructible_v<_Tp, _Args...>) */
namespace n {
template <class A, class B> concept conv = __is_convertible(A, B);
template <class T, class... A>
constexpr bool cons = __is_constructible(T, A...);
template <class R> struct view {
    R *p;
    template <class T>
        requires conv<T, R &> && conv<R &, R &>
    view(T &&t) : p(&t) {}
    template <class T>
    int f(T)
        requires conv<T, view<R>> || conv<T, R>
    {
        return 1;
    }
    template <class T>
    int g(T)
        requires conv<T, const R>
    {
        return 2;
    }
};
template <class T> struct box {
    T v;
    template <class... A>
        requires cons<T, A...>
    box(int, A &&...a) : v(a...) {}
};
}

int main()
{
    int x = 4;
    n::view<int> v(x);
    n::box<long> b(0, 5L);
    return v.f(1) * 10 + v.g(2) + *v.p + static_cast<int>(b.v) - 21;
}
