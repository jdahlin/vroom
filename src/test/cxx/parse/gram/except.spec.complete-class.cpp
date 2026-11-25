// RUN: %clang_cc1 -std=c++11 -Wno-dynamic-exception-spec %s
// RUN: %clang_cc1 -std=c++14 -Wno-dynamic-exception-spec %s
// [class.mem]/6 (C++14's /2 names the exception-specification): a
// dynamic-exception-specification's type-ids are read with the class
// complete, so they may name a member a later declaration makes
// (CWG1330).
struct A {
  void f() throw(T) {}
  void g() throw(T, U) {}
  struct T {};
  typedef int U;
};
template <typename V> struct B {
  void f() throw(T, typename V::type) {}
  struct T {};
};
void (A::*pf)() throw(A::T) = &A::f;
