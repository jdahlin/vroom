/* [temp.alias]/4 (CWG 1558): void_t<typename T::type> is void, but its
   argument is still substituted, a substitution failure for T without a
   member type; refused rather than folded to void too early */
template <class...> using void_t = void;
template <class T, class = void> struct has_type {
    static const bool value = false;
};
template <class T> struct has_type<T, void_t<typename T::type>> {
    static const bool value = true;
};
struct A {
    typedef int type;
};
static_assert(has_type<A>::value && !has_type<int>::value, "");
