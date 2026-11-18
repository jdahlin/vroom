/* [temp.class.spec.match]/3, [temp.alias]/4 (CWG 1558): the void_t
   detection idiom.  void_t<typename T::type> is void, but its argument is
   substituted when the partial specialization is matched: a substitution
   failure, so no match, for a T without the member */
template <class...> using void_t = void;
template <class T> using always_void = void;

template <class T, class = void> struct has_type {
    static const bool value = false;
};
template <class T> struct has_type<T, void_t<typename T::type>> {
    static const bool value = true;
};

template <class T, class = void> struct has_size_type {
    enum { value = 0 };
};
template <class T> struct has_size_type<T, always_void<typename T::size_type>> {
    enum { value = 1 };
};

struct A {
    typedef int type;
    typedef unsigned size_type;
};
struct B {
    int type;
};
static_assert(has_type<A>::value && !has_type<int>::value, "");
static_assert(!has_type<B>::value, "B::type is no type");
static_assert(has_size_type<A>::value == 1 && has_size_type<B>::value == 0,
              "");

int main()
{
    return has_type<A>::value + has_size_type<long>::value - 1;
}
