// [abi:mangle.decltype], [abi:mangle.template-arg] template arguments written as type-ids that are
// not names in a dependent decltype (std::declval<_Iter&>(), std::declval<__deref_t<_Iter>>()),
// in a function template's return type and its parameters' types (libc++'s
// _IterOps::__iter_move: __enable_if_t<is_reference<__deref_t<_Iter>>::value, int>, __move_t<_Iter>)
namespace std {
template <class T> T &&declval();
template <class T> struct remove_reference { typedef T type; };
template <class T> struct remove_reference<T &> { typedef T type; };
template <class T> typename remove_reference<T>::type &&move(T &&t) { return static_cast<typename remove_reference<T>::type &&>(t); }
template <class T> struct is_reference { static constexpr bool value = false; };
template <class T> struct is_reference<T &> { static constexpr bool value = true; };
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
}
template <class I> using deref_t = decltype(*std::declval<I &>());
template <class I> using move_t = decltype(std::move(std::declval<deref_t<I>>()));
struct ops {
  template <class I, typename std::enable_if<std::is_reference<deref_t<I>>::value, int>::type = 0>
  static move_t<I> iter_move(I &&i) { return std::move(*i); }
};
int main() {
  int a = 3;
  int *p = &a;
  int &&r = ops::iter_move(p);
  return r == 3 ? 0 : 1;
}
