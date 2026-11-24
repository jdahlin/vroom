// RUN: %clang_cc1 -std=c++20 -fsyntax-only %s
// [temp.friend]/4: a template friend class that is a member of a dependent
// type (A<U *>::B).  clang does not support the form: it turns off access
// control for the granting class (-Wunsupported-friend), so A<int>::f is
// accessible from A<double>::B<int> and from a class that is no friend
template <class T>
struct A {
  template <class> struct B;
private:
  static void f();
  template <class U> friend struct A<U *>::B; // expected-warning {{not supported}}
};
template <class T>
template <class U> struct A<T>::B {
  static void g() { A<int>::f(); }
};
void test() { A<double>::B<int>::g(); }
struct Z { void z(); };
void Z::z() { A<char>::f(); }
