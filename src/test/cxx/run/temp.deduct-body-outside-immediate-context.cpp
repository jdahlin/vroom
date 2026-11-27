/* [temp.deduct]/8: a definition instantiated while a candidate's type is
   substituted (a deduced return type) is outside the immediate context:
   its body names a member with a pack expansion in explicit template
   arguments, as libc++'s variant dispatch does. */
extern "C" int printf(const char *, ...);
template <int... Is> struct D {
    template <class F, class... Ts> static constexpr int disp(F f)
    {
        return f((Is + ... + static_cast<int>(sizeof...(Ts))));
    }
};
template <class F, class... Ts> constexpr decltype(auto) dispatch()
{
    return &D<1, 2>::template disp<F, Ts...>;
}
template <class F> decltype(auto) run(F f)
{
    constexpr auto p = dispatch<F, int, long>();
    return p(f);
}
struct V {
    int operator()(int i) const { return i * 10; }
};
template <class F> auto call(F f) -> decltype(run(f) + 1) { return run(f) + 1; }
int main() { printf("%d %d\n", call(V{}), run(V{})); }
