// RUN: %clang_cc1 -std=c++14 -fsyntax-only -verify %s
// [basic.life]/1, [expr.const]/5.8: before C++20 an assignment to a member
// from its own default member initializer is to an object outside its lifetime
struct D {
  int x = x = 3; // expected-note {{assignment to object outside its lifetime}}
  constexpr D() {}
};
static_assert(D().x == 3, ""); // expected-error {{not an integral constant expression}} expected-note {{in call to}} expected-note {{temporary created here}}
