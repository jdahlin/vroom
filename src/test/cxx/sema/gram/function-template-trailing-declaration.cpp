// RUN: %clang_cc1 -std=c++11 -fsyntax-only %s
// [dcl.spec.auto.general]/2, [temp.fct]/1: a function template declared
// (not defined) with auto and a trailing-return-type is a function
// template's declaration, not a variable template's.
template <class T> struct A {};
template <class T> auto h() -> A<T>;
template <class T> auto k(T t) -> decltype(t + 1);
template <class T> auto k(T t) -> decltype(t + 1) { return t + 1; }
int f() { return k(1); }
