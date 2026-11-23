/* Attributes before a declaration, as libc++ writes them: [[nodiscard]]
   (an attribute without effect) before a function and a function
   template, and _LIBCPP_HIDE_FROM_ABI's GCC attributes before a
   conversion function of a class template */
template <class T> [[nodiscard]] T twice(T t) noexcept
{
    return t + t;
}
[[nodiscard]] int one()
{
    return 1;
}
template <class T, T v> struct constant {
    typedef T value_type;
    __attribute__((__visibility__("hidden")))
    __attribute__((__exclude_from_explicit_instantiation__)) constexpr
    operator value_type() const noexcept
    {
        return v;
    }
};
int main()
{
    constant<int, 3> c;
    int three = c;
    if (twice(3) != 6 || one() != 1 || three != 3)
        return 1;
    return 0;
}
