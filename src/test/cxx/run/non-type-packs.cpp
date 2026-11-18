/* [temp.param]/17: non-type template parameter packs (int... N), the
   primary's last parameter; [temp.class.spec.match] with
   [temp.deduct.type]/9: partial specializations of a variadic primary
   match the pack element by element, their own pack (In...) taking the
   rest (libc++'s __static_max); sizeof...; an alias template that
   forwards its pack (aligned_union_t's form) */
template <unsigned long I0, unsigned long... In> struct static_max;
template <unsigned long I0> struct static_max<I0> {
    static const unsigned long value = I0;
};
template <unsigned long I0, unsigned long I1, unsigned long... In>
struct static_max<I0, I1, In...> {
    static const unsigned long value = I0 >= I1
                                           ? static_max<I0, In...>::value
                                           : static_max<I1, In...>::value;
};
template <int... N> struct count {
    static const int value = sizeof...(N);
};
template <class... T> struct types {};
template <class T, class... U> struct types_tail {
    static const int value = 1 + sizeof...(U);
};
template <class... T> struct types_count;
template <> struct types_count<> {
    static const int value = 0;
};
template <class H, class... T> struct types_count<H, T...> {
    static const int value = 1 + types_count<T...>::value;
};
template <unsigned long L, class T0, class... T> struct sized {
    typedef char type[L + sizeof(T0) + sizeof...(T)];
};
template <unsigned long L, class... T>
using sized_t = typename sized<L, T...>::type;

static_assert(static_max<3, 9, 4>::value == 9, "");
static_assert(static_max<7>::value == 7, "");
static_assert(count<1, 2, 3>::value == 3 && count<>::value == 0, "");
static_assert(types_count<int, char, long>::value == 3, "");
static_assert(sizeof(sized_t<1, int, char>) == 6, "");

int main()
{
    return static_max<2, 5, 1, 4>::value - 5 + types_count<>::value;
}
