/* [ext:clang_builtin_templates] [abi:expressions] tl 18__make_integer_seqI
   ... for an alias of __make_integer_seq<B, T, N> braced in a return type,
   N dependent (libc++'s make_index_sequence<N> in json's from_json) */
extern "C" int printf(const char *, ...);

namespace lib
{
template <class T, T... I> struct seq {
};

template <template <class U, U...> class B, class T, T N>
using make_impl = __make_integer_seq<B, T, N>;

template <class T, T N> using make_seq = make_impl<seq, T, N>;
template <unsigned long N> using make_index = make_seq<unsigned long, N>;
} // namespace lib

template <class T, unsigned long... I>
int count(T, lib::seq<unsigned long, I...>)
{
    return sizeof...(I);
}

template <unsigned long N>
auto f(int j) -> decltype(count(j, lib::make_index<N>{}))
{
    return count(j, lib::make_index<N>{});
}

int main()
{
    printf("%d\n", f<3>(0));
    return 0;
}
