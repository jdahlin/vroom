// [abi:mangle.template-arg] a non-type template parameter's dependent type naming
// alias<args>::value (clang mangles the alias template as written: json's
// detect_string_can_append<OutStringType, Arg>::value in concat_into) and
// Q<args>::template f<args>() (libc++'s pair(U1&&, U2&&):
// __check_pair_construction<_T1, _T2>::template __is_pair_constructible<_U1, _U2>())
template <bool B, class T = void> struct en {};
template <class T> struct en<true, T> { using type = T; };
template <bool B, class T = void> using en_t = typename en<B, T>::type;
template <class T> T &&dv();
template <class...> using void_t = void;
template <class D, template <class...> class Op, class... A> struct det {
  static constexpr bool value = false;
};
template <template <class...> class Op, class... A> struct det<void_t<Op<A...>>, Op, A...> {
  static constexpr bool value = true;
};
template <template <class...> class Op, class... A> using is_det = det<void, Op, A...>;
template <class S, class A> using can_app = decltype(dv<S &>().append(dv<A>()));
template <class S, class A> using det_app = is_det<can_app, S, A>;
struct Str {
  int n = 0;
  void append(int i) { n += i; }
};
template <class S, class A, en_t<det_app<S, A>::value, int> = 0> int c1(S &s, A &&a) {
  s.append(a);
  return 1;
}
template <class S, class A, en_t<!det_app<S, A>::value, int> = 0> int c1(S &, A &&) { return 2; }
template <class A, class B> struct chk {
  template <class U, class V> static constexpr bool ok() { return sizeof(U) == sizeof(A) && sizeof(V) == sizeof(B); }
};
template <class A, class B> struct pr {
  int k;
  template <class U, class V, en_t<chk<A, B>::template ok<U, V>(), int> = 0> pr(U &&, V &&) : k(1) {}
};
int main() {
  Str s;
  pr<int, int> p(1, 2);
  return c1(s, 3) == 1 && s.n == 3 && c1(s, &s) == 2 && p.k == 1 ? 0 : 1;
}
