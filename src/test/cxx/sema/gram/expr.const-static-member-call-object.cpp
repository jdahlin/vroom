// RUN: %clang_cc1 -std=c++11 -fsyntax-only -verify %s
// [expr.ref]/6.3.1: a call of a static member function named through an
// object expression, `s.f(19)`, is a constant expression when the function
// is: the object expression is evaluated and designates its object, none of
// its value is read.
struct S {
  static constexpr int k = 42;
  static constexpr int f(int n) { return n * k + 2; }
} s;
constexpr int n = s.f(19);
static_assert(n == 800, "");
static_assert(s.f(19) == 800, "");
static_assert(S::f(19) == 800, "");
// expected-no-diagnostics
