// RUN: %clang_cc1 -std=c++11 -fsyntax-only %s
// [dcl.constexpr]/7: a constexpr member function of a class template whose
// specialization fails the requirements (a non-literal return or
// parameter type) is still constexpr, not ill-formed, and its use is not
// (CWG1358)
struct NonLit { NonLit(); operator int(); };
template <typename T, typename U> struct A {
  int member;
  constexpr A(U u) : member(u) {}
  constexpr T f(U u) const { return T(); }
};
A<NonLit, NonLit> a = NonLit();
void g() { a.f(NonLit()); }
// expected-no-diagnostics
