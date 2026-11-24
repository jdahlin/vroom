// RUN: %clang_cc1 -std=c++11 -fsyntax-only %s
// [dcl.fct]/2: auto before a trailing-return-type declarator is a valid
// type-id, abstract declarators too (clang: CXX/dcl.dcl/dcl.spec.auto/p2.cpp).
template <typename T> struct W {};
void j() {
  (void)sizeof(auto (*)()->void);
  W<auto (*)()->void> v;
  (void)v;
  (void)(auto (*)()->void)(&j);
}
