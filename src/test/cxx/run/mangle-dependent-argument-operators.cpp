/* [abi:mangle.template-arg] X <expression> E of a value-dependent
   template-argument (enable_if's condition in a default template argument
   or a return type), as Skia's and libc++'s templates write it:
   - each relational and equality operator (lt le ge eq ne, gt before);
   - a static data member that is no longer dependent once the enclosing
     class template's arguments are known (fp<R>::value in D<long>), an
     external name L_Z <encoding> E, negated with nt;
   - enable_if in an inline namespace std::__1 as the <prefix> of
     enable_if<...>::type: St 3__1 9enable_if <args> 4type */
namespace std {
inline namespace __1 {
template <bool B, class T = void> struct enable_if {
};
template <class T> struct enable_if<true, T> {
    using type = T;
};
template <bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;
template <class T> inline constexpr bool is_fp_v = false;
template <> inline constexpr bool is_fp_v<float> = true;
template <> inline constexpr bool is_fp_v<double> = true;
} // namespace __1
} // namespace std

template <class T> struct fp {
    static const bool value = false;
};
template <> struct fp<double> {
    static const bool value = true;
};

template <class R, std::enable_if_t<sizeof(R) <= 4, int> = 0> int le(R)
{
    return 1;
}
template <class R, std::enable_if_t<(sizeof(R) < 8), int> = 0> int lt(R)
{
    return 2;
}
template <class R, std::enable_if_t<(sizeof(R) >= 8), int> = 0> int ge(R)
{
    return 3;
}
template <class R, std::enable_if_t<sizeof(R) == 4, int> = 0> int eq(R)
{
    return 4;
}
template <class R, std::enable_if_t<sizeof(R) != 4, int> = 0> int ne(R)
{
    return 5;
}
template <class T> std::enable_if_t<(sizeof(T) > 2), int> returned(T)
{
    return 6;
}
template <class T, std::enable_if_t<std::is_fp_v<T>, bool> = true>
int finite(T x)
{
    return x == x;
}

template <class R> struct D {
    R r;
    template <class R2,
              std::enable_if_t<fp<R>::value || !fp<R2>::value, int> = 0>
    explicit D(const R2 &x) : r(x)
    {
    }
};

int main()
{
    if (le(1) != 1 || lt('c') != 2 || ge(1L) != 3 || eq(1) != 4 ||
        ne(1L) != 5 || returned(1) != 6)
        return 1;
    if (!finite(1.5f) || !finite(2.5))
        return 2;
    D<long> d(7);
    D<double> e(1.5);
    if (d.r != 7 || e.r != 1.5)
        return 3;
    return 0;
}
