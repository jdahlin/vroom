/* [class.mem]/6: a member function's noexcept-specifier is a
   complete-class context: it names the class being defined, which is
   complete there ([except.spec]/1).  [temp.func.order]: partial ordering of
   function templates whose parameter types are class templates with
   non-type arguments. */
template <class T> struct trait {
    static constexpr bool value = __is_constructible(T, T &&);
};
template <class T> struct leaf {
    T value;
    int swap_with(leaf &o) noexcept(trait<leaf>::value) { return o.value; }
    int copy_with(leaf &o) noexcept(!trait<leaf>::value) { return o.value; }
};
static_assert(noexcept(static_cast<leaf<int> *>(nullptr)->swap_with(
    *static_cast<leaf<int> *>(nullptr))));
static_assert(!noexcept(static_cast<leaf<int> *>(nullptr)->copy_with(
    *static_cast<leaf<int> *>(nullptr))));

template <class T> int order(T &, T &) { return 1; }
template <unsigned long I, class H, bool E> struct tagged {
    H value;
};
template <unsigned long I, class H, bool E>
int order(tagged<I, H, E> &, tagged<I, H, E> &)
{
    return 2;
}

int main()
{
    leaf<int> a{1}, b{2};
    tagged<0, int, false> t{3}, u{4};
    int plain_int = 5;
    if (a.swap_with(b) != 2 || a.copy_with(b) != 2)
        return 1;
    if (order(t, u) != 2 || order(plain_int, plain_int) != 1)
        return 2;
    return 0;
}
