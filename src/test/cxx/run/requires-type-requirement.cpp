/* [expr.prim.req.type]: a concept's requires-expression of
   type-requirements, false when a type is invalid ([expr.prim.req]/6);
   libc++'s __has_member_difference_type in incrementable_traits.h */
template <class T>
concept has_d = requires { typename T::difference_type; };
template <class T>
concept has_both = requires {
    typename T::difference_type;
    typename T::value_type;
};
template <class T> struct I { static constexpr int v = 0; };
template <has_d T> struct I<T> {
    using difference_type = typename T::difference_type;
    static constexpr int v = sizeof(difference_type);
};
struct A {
    using difference_type = long;
};
struct B {
    using difference_type = char;
    using value_type = int;
};
template <class T> using with_ref = T&;
template <class T>
concept can_ref = requires { typename with_ref<T>; };
template <class T> struct Probe {
    static constexpr bool has_d = requires { typename T::difference_type; };
};
static_assert(can_ref<int>);
static_assert(!can_ref<void>);
static_assert(Probe<A>::has_d);
static_assert(!Probe<int>::has_d);
int main()
{
    return I<A>::v * 10 + I<int>::v + I<B>::v + has_both<B> * 100 +
           has_both<A> * 1000 - 181;
}
