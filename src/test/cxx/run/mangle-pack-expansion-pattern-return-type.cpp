// [abi:mangle.function-type], [abi:mangle.template-arg] a pack expansion whose pattern is a type-id
// in a template argument of a function template's return type: J Dp <type> E, a substitution
// candidate (libc++'s forward_as_tuple: tuple<_Tp&&...>)
template <class... T> struct tup {
  int n;
  tup(T... t) : n(sizeof...(T)) {}
};
template <class... T> tup<T &&...> fat(T &&...t) { return tup<T &&...>(static_cast<T &&>(t)...); }
int main() {
  int a = 1;
  long b = 2;
  return fat(a, b).n == 2 && fat(3).n == 1 ? 0 : 1;
}
