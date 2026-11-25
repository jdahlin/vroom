// [abi:expressions] a call of T::f with an argument that calls f<args> named unqualified, in a
// function template's decltype return type: cl sr <T> <f> cl <f> I <args> E E E (libc++'s
// __unwrap_iter: decltype(_Impl::__unwrap(std::declval<_Iter>())))
template <class T> T &&declval();
template <class I, bool = false> struct impl {
  static I unwrap(I i) noexcept { return i; }
};
template <class I, class Impl = impl<I> > decltype(Impl::unwrap(declval<I>())) unwrap_iter(I i) noexcept {
  return Impl::unwrap(i);
}
template <class A, class B> int pair_of(decltype(declval<A>() + declval<B &>())) { return 3; }
int main() {
  int a[2] = {0, 0};
  int *p = unwrap_iter(a + 0);
  return *p + pair_of<int, long>(1) == 3 ? 0 : 1;
}
