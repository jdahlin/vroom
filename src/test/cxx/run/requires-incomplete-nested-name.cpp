// [expr.prim.req]/5: in a requires-expression, a nested-name-specifier
// naming a class template specialization the template has no definition
// for (lib::tuple_size<int>) makes the requirement unsatisfied, as SkSpan's
// compute_extent tests lib::tuple_size<U>::value.
namespace lib {
template <class T> struct tuple_size;
template <class T, int N> struct array {
    T e[N];
};
template <class T, int N> struct tuple_size<array<T, N>> {
    static constexpr int value = N;
};
} // namespace lib
template <class T> struct ts;
template <class U> constexpr int f() { if constexpr (requires { ts<U>::value; }) return 1; else return 2; }
static_assert(f<int>() == 2);
template <class U> constexpr bool g() { return requires { lib::tuple_size<U>::value; }; }
static_assert(!g<int>());
static_assert(g<lib::array<int, 3>>());

int main()
{
    static_assert(f<long>() == 2 && !g<char>());
    return 0;
}
