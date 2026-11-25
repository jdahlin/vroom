// [abi:expressions] Q::f<args>() and Q::name with Q a template type parameter of a constructor
// template of a class template's specialization (libc++'s pair(_T1 const&, _T2 const&):
// _CheckArgsDep::template __is_pair_constructible<_T1 const&, _T2 const&>()): cl sr T_ <name>
// <template-args> E, the class's parameters substituted (T1 is int, not T_); symbols = clang's

template <bool B> struct bc { static constexpr bool value = B; };
typedef bc<true> yes;
typedef bc<false> no;
template <bool C, class R = void> struct en {};
template <class R> struct en<true, R> { typedef R type; };
template <bool C, class R = void> using en_t = typename en<C, R>::type;
template <class A, class B> struct same : no {};
template <class A> struct same<A, A> : yes {};
template <class... T> struct And { static constexpr bool value = true; };
template <class T> struct holder { typedef T type; };
template <class A, class B> struct check {
  template <class U, class V> static constexpr bool ok() { return true; }
  template <class U> static constexpr bool one() { return true; }
  static constexpr bool plain() { return true; }
  static constexpr bool value = true;
};

namespace np01 {
template <class T1, class T2> struct pr {
  int k;
  template <class C = check<T1, T2>, en_t<C::value, int> = 0> pr(T1 const&, T2 const&) : k(1) {}
};
int run() { pr<int, long> p(1, 2L); return p.k - 1; }
} // namespace np01

namespace np02 {
template <class T1, class T2> struct pr {
  int k;
  template <class C = check<T1, T2>, en_t<C::plain(), int> = 0> pr(T1 const&, T2 const&) : k(1) {}
};
int run() { pr<int, long> p(1, 2L); return p.k - 1; }
} // namespace np02

namespace np03 {
template <class T1, class T2> struct pr {
  int k;
  template <class C = check<T1, T2>, en_t<C::template one<int>(), int> = 0> pr(T1 const&, T2 const&) : k(1) {}
};
int run() { pr<int, long> p(1, 2L); return p.k - 1; }
} // namespace np03

namespace np04 {
template <class T1, class T2> struct pr {
  int k;
  template <class C = check<T1, T2>, en_t<C::template one<T1>(), int> = 0> pr(T1 const&, T2 const&) : k(1) {}
};
int run() { pr<int, long> p(1, 2L); return p.k - 1; }
} // namespace np04

namespace np05 {
template <class T1, class T2> struct pr {
  int k;
  template <class C = check<T1, T2>, en_t<C::template one<T1 const&>(), int> = 0> pr(T1 const&, T2 const&) : k(1) {}
};
int run() { pr<int, long> p(1, 2L); return p.k - 1; }
} // namespace np05

namespace np06 {
template <class T1, class T2> struct pr {
  int k;
  template <class C = check<T1, T2>, en_t<C::template ok<T1 const&, T2 const&>(), int> = 0> pr(T1 const&, T2 const&) : k(1) {}
};
int run() { pr<int, long> p(1, 2L); return p.k - 1; }
} // namespace np06

namespace np07 {
template <class T1, class T2> struct pr {
  int k;
  template <class C = check<T1, T2>, en_t<check<T1, T2>::template ok<T1 const&, T2 const&>(), int> = 0> pr(T1 const&, T2 const&) : k(1) {}
};
int run() { pr<int, long> p(1, 2L); return p.k - 1; }
} // namespace np07

int main() {
  int failed = 0;
  failed += np01::run() != 0;
  failed += np02::run() != 0;
  failed += np03::run() != 0;
  failed += np04::run() != 0;
  failed += np05::run() != 0;
  failed += np06::run() != 0;
  failed += np07::run() != 0;
  return failed;
}
