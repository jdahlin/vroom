/* [ext:clang_builtin_templates] __make_integer_seq<B, T, N>: B<T, 0, 1,
   ..., N - 1>, directly and through an alias template of dependent
   arguments (libc++'s make_integer_sequence) */
template <class T, T... V> struct seq {
    static constexpr unsigned size = sizeof...(V);
    static T sum() { return (V + ... + T(0)); }
    static T weighted() { return ((V * V) + ... + T(0)); }
};

template <class A, class B> struct same {
    static constexpr bool value = false;
};
template <class A> struct same<A, A> {
    static constexpr bool value = true;
};

static_assert(same<__make_integer_seq<seq, int, 4>, seq<int, 0, 1, 2, 3>>::value,
              "");
static_assert(same<__make_integer_seq<seq, long, 0>, seq<long>>::value, "");

template <template <class T, T...> class B, class T, T N>
using make_impl = __make_integer_seq<B, T, N>;
template <class T, T N> using make_seq = make_impl<seq, T, N>;
template <unsigned long N> using make_index = make_seq<unsigned long, N>;

static_assert(same<make_seq<short, 3>, seq<short, 0, 1, 2>>::value, "");

int main()
{
    if (make_index<5>::size != 5)
        return 1;
    if (make_index<5>::sum() != 10)
        return 2;
    if (make_seq<int, 4>::weighted() != 14)
        return 3;
    if (__make_integer_seq<seq, unsigned char, 7>::sum() != 21)
        return 4;
    return 0;
}
