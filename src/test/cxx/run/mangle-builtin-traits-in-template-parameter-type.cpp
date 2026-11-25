// [abi:expressions] a value kept as written in a non-type template parameter's dependent type,
// with the names an alias template bound in it: a clang type trait as a vendor expression,
// u9__is_same <type> <type> E, of a type transformation (u14__remove_cvref I T_ E) and a pack
// argument (J ... E) (libc++'s __tuple_leaf(_Tp&&): _IsNotSame<__remove_cvref_t<_Tp>,
// __tuple_leaf>, is_constructible<_Hp, _Tp> in __enable_if_t<_And<...>::value, int>)
template <class T, T v> struct ic { static constexpr T value = v; };
template <bool B> using bc = ic<bool, B>;
template <bool B, class T = void> struct en {};
template <class T> struct en<true, T> { using type = T; };
template <bool B, class T = void> using en_t = typename en<B, T>::type;
template <class T, class U> using not_same = bc<!__is_same(T, U)>;
template <class T> using rcv = __remove_cvref(T);
template <class... P> struct all { static constexpr bool value = (P::value && ...); };
template <class T, class... A> struct ctor { static constexpr bool value = __is_constructible(T, A...); };
template <int I, class H> struct leaf {
  H v;
  template <class T, en_t<all<not_same<rcv<T>, leaf>, ctor<H, T>>::value, int> = 0>
  explicit leaf(T &&t) : v(t) {}
};
template <class H> struct plain {
  H v;
  template <class T, en_t<all<not_same<T, H>>::value, int> = 0> explicit plain(T &&t) : v(t) {}
};
int main() {
  leaf<0, long> l(3);
  plain<long> p(4);
  return l.v == 3 && p.v == 4 ? 0 : 1;
}
