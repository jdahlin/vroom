// Parameter packs: a pack bound to the remaining arguments, expansions re-parsed per element in
// template arguments and base clauses (leaf<Is, Ts>...), sizeof..., a template template argument,
// __make_integer_seq, __type_pack_element, a trailing pack in a pattern, a pack inside a nested pattern.
// A > before = is the operator >=, not the end of the arguments.
namespace n {
template <class... Ts> struct list { static const int size = sizeof...(Ts); char tag[sizeof...(Ts) + 1]; };
template <class T, class... Rest> struct rec { T head; rec<Rest...> tail; };     // recursive tuple
template <class T> struct rec<T> { T head; };                                    // the last one
template <unsigned long I, class T> struct leaf { T value; };
template <class T, T... Is> struct iseq {};
template <class Seq, class... Ts> struct impl;
template <unsigned long... Is, class... Ts> struct impl<iseq<unsigned long, Is...>, Ts...> : leaf<Is, Ts>... {};
template <class... Ts> struct tup : impl<__make_integer_seq<iseq, unsigned long, sizeof...(Ts)>, Ts...> {};
template <template <class> class W, class T> struct apply { W<T> w; };
template <class T> struct box { T v; double d; };
template <class... Ts> struct second { __type_pack_element<1, Ts...> x; };
template <class T, class... Rest> struct first_or { char c; };
template <class... Rest> struct first_or<int, Rest...> { long l; };              // pattern: a trailing pack
}
n::list<int, char, double> l3; n::list<> l0;
n::rec<char, int, double> r3;
n::tup<char, int, double> t3; n::tup<> t0;
n::apply<n::box, short> ap;
n::second<char, double, int> s2;
n::first_or<int, char, char> f1; n::first_or<char, int> f2; n::first_or<int> f3;
namespace n { template <bool B> struct boolc { char c; }; template <class... Ts> struct nonempty { boolc<sizeof...(Ts) >= 1> b; }; }
n::nonempty<int> ne;
