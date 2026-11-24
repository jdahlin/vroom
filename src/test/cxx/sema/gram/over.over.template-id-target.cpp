// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// [over.over]/1-3 with [temp.deduct.funcaddr]: a template-id (qualified or
// not) of several function templates, not called, initializing a function
// pointer or reference: each template's explicit arguments, then deduction
// from the target's type; the specialization whose type is the target's
namespace N {
template <class T> struct rr { using type = T; };
template <class T> T &&fw(typename rr<T>::type &x) { return static_cast<T &&>(x); }
template <class T> T &&fw(typename rr<T>::type &&x) { return static_cast<T &&>(x); }
template <class T> T *f(T &);
template <class T> T *f(T &&);
template <class T> int g(T &) {
  static_assert(T::value, ""); // expected-error {{no member named 'value' in 'B'}}
  return 0;
}
template <class T> int g(T &&) { return 0; }
}
struct B {};
B &&(*p1)(B &) = &N::fw<B>;
B &&(*p2)(B &&) = N::fw<B>;
B &&(&r1)(B &) = N::fw<B>;
B *(*p3)(int &) = &N::f<B>; // expected-error {{address of overloaded function 'f' does not match required type 'B *(int &)'}} expected-note@10 {{candidate template ignored}} expected-note@11 {{candidate template ignored}}
int (*p4)(B &&) = &N::g<B>;
int (*p5)(B &) = &N::g<B>; // expected-note {{in instantiation of}}
