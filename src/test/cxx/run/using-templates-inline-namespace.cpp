// [namespace.udecl]/1: a using-declarator naming function templates declared
// in a namespace with an inline namespace (a using-directive,
// [namespace.def]/7) that declares nothing of the name: libc++'s
// `using ranges::get;` in __fwd/subrange.h
namespace s {
namespace r {
inline namespace cpo {
inline constexpr int other = 1;
}
template <class I, class S> struct sub { I i; S s; };
template <unsigned long N, class I, class S>
constexpr I get(const sub<I, S> &);
template <unsigned long N, class I, class S>
constexpr S get(sub<I, S> &);
template <unsigned long N, class I, class S>
constexpr I get(const sub<I, S> &x) { return x.i; }
template <unsigned long N, class I, class S>
constexpr S get(sub<I, S> &x) { return x.s; }
}
using r::get;
}
int main()
{
    s::r::sub<int, long> x{3, 4};
    if (s::get<0>(x) != 3) return 1;
    if (s::get<1>(x) != 4) return 2;
    return 0;
}
