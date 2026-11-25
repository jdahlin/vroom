// [abi:expressions] sizeof...(P) in a non-type template parameter's dependent type: sZ <template-param>,
// P the function template's pack or the class template's (clang: the parameter's position alone);
// libc++'s tuple(_Up&&...): _BoolConstant<sizeof...(_Up) == sizeof...(_Tp)> beside a member class
// template _EnableUTypesCtor<_Up...>
template <bool B> struct bc { static constexpr bool value = B; };
template <class... T> struct And { static constexpr bool value = (T::value && ...); };
template <bool C, class R = void> struct en {};
template <class R> struct en<true, R> { typedef R type; };
template <bool C, class R = void> using en_t = typename en<C, R>::type;
template <class... Up, en_t<sizeof...(Up) == 2, int> = 0> int f(Up &&...) { return 2; }
template <class X, class... Tp> struct tuple {
  int n;
  template <class... Up> struct Enable : bc<true> {};
  template <class... Up, en_t<And<bc<sizeof...(Up) == sizeof...(Tp)>, Enable<Up...> >::value, int> = 0>
  tuple(Up &&...) : n(sizeof...(Up)) {}
  template <class Y, class... Up, en_t<bc<sizeof...(Up) + 1 == sizeof...(Tp) + 1>::value, int> = 0>
  tuple(Y, Up &&...) : n(10) {}
};
int main() {
  tuple<char, int, long> t(1, 2L);
  tuple<char, int, long> u(nullptr, 1, 2L);
  return f(1, 2L) == 2 && t.n == 2 && u.n == 10 ? 0 : 1;
}
