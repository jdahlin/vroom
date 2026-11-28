/* [temp.alias]: a non-type parameter of an alias template, bound to its
   dependent default, in the mangled return type (fmt's convert_float). */
template <class T> constexpr int bits() { return sizeof(T) * 8; }
template <bool B, class A, class C> struct cond {
    using type = A;
};
template <class A, class C> struct cond<false, A, C> {
    using type = C;
};
template <bool B, class A, class C>
using cond_t = typename cond<B, A, C>::type;
template <class A, class B> struct same {
    static constexpr bool value = false;
};
template <class A> struct same<A, A> {
    static constexpr bool value = true;
};

template <typename T, bool doublish = bits<T>() == bits<double>()>
using result = cond_t<same<T, float>::value || doublish, double, T>;

template <typename T> constexpr auto convert(T value) -> result<T>
{
    return static_cast<result<T>>(value);
}

int main()
{
    volatile float a = 1.0f;
    volatile long double b = 2.0L;
    float x = a;
    long double y = b;
    return convert(x) != 1.0 || convert(y) != 2.0L;
}
