/* [ext:clang_builtin_templates] __type_pack_element<N, T...>: the N-th
   type of the pack, as libc++'s tuple_element uses it (dependent in the
   partial specialization, substituted on instantiation) */
typedef unsigned long size_t;
namespace s {
template <class... T> struct tuple {};
template <size_t I, class T> struct tuple_element;
template <size_t I, class... T> struct tuple_element<I, tuple<T...>> {
    using type = __type_pack_element<I, T...>;
};
template <size_t I, class T>
using tuple_element_t = typename tuple_element<I, T>::type;
} // namespace s

struct big {
    char c[24];
};
typedef s::tuple<char, short, big, double> t4;
static_assert(sizeof(s::tuple_element_t<0, t4>) == 1, "");
static_assert(sizeof(s::tuple_element_t<1, t4>) == 2, "");
static_assert(sizeof(s::tuple_element_t<2, t4>) == 24, "");
static_assert(sizeof(__type_pack_element<1, int, long>) == 8, "");

/* a dependent use in a function template's body */
template <size_t I, class... T> long first(T... v)
{
    __type_pack_element<I, T...> r = 0;
    return r + sizeof...(v) + sizeof r * 100;
}

extern "C" int printf(const char *, ...);
int main()
{
    s::tuple_element_t<3, t4> d = 2.5;
    s::tuple_element_t<1, t4> h = 7;
    printf("%d %d %d\n", (int)(d * 2), h,
           (int)sizeof(s::tuple_element_t<2, t4>));
    printf("%ld %d\n", first<1>(3, 40L, 'c'),
           (int)first<2>(3, 4L, 'c'));
    return 0;
}
