/* [ext:clang_builtin_templates], [abi:expressions]: tl <type> E for
   std::make_index_sequence<N>{} of a dependent N, the type
   __make_integer_seq<std::integer_sequence, std::size_t, N> */
/* declared here, not <utility>: the asan job has no C++ library headers */
namespace std
{
template <class T, T... I> struct integer_sequence {
};
template <unsigned long... I>
using index_sequence = integer_sequence<unsigned long, I...>;
template <unsigned long N>
using make_index_sequence =
    __make_integer_seq<integer_sequence, unsigned long, N>;
} // namespace std
template <class T, unsigned long N> struct A { T v[N]; };
template <class T, unsigned long N> struct tag {};
template <class T, unsigned long... I> T impl(const A<T, sizeof...(I)> &s, std::index_sequence<I...>) { return s.v[0]; }
template <class T, unsigned long N>
auto from(const A<T, N> &s, tag<T, N> t) -> decltype(impl(s, std::make_index_sequence<N>{}))
{
    return impl(s, std::make_index_sequence<N>{});
}
int main() { A<int, 2> s{{3, 4}}; return from(s, tag<int, 2>{}) - 3; }
