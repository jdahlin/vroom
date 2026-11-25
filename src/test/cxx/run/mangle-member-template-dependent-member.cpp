// [abi:mangle.name] typename get<Y>::type, get a member class template of the class template
// whose member function template names it: N <prefix of the class> <get> <template-args>
// <name> E, in a parameter type and in a non-type template parameter's type (libc++'s
// shared_ptr::__enable_weak_this: typename __get_shared_from_this<...>::type)
template <bool C, class R = void> struct en {};
template <class R> struct en<true, R> { typedef R type; };
template <bool C, class R = void> using en_t = typename en<C, R>::type;
template <class A, class B> struct conv { static constexpr bool value = false; };
template <class T> struct sp {
  template <class V, class = void> struct get { using type = int; };
  template <class Y> int ew(Y, typename get<Y>::type n) { return n; }
  template <class Y, class O, en_t<!conv<O, typename get<Y>::type>::value, int> = 0> int ew2(Y, O) {
    return 3;
  }
};
int main() {
  sp<int> s;
  return s.ew((int *)0, 2) == 2 && s.ew2((int *)0, (int *)0) == 3 ? 0 : 1;
}
