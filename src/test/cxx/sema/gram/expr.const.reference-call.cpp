// RUN: %clang_cc1 -std=c++17 -fsyntax-only %s
// [expr.call]/14 with [expr.const]/5.2: a call that returns a reference is
// a glvalue whose evaluation calls the function: a constexpr function whose
// every invocation calls a non-constexpr one never produces a constant
// expression ([dcl.constexpr]/6, clang's -Winvalid-constexpr)
int &h(int &); // expected-note {{declared here}}
constexpr bool k(int a) { h(a); return true; } // expected-error {{never produces a constant expression}} expected-note {{non-constexpr function 'h'}}
constexpr const int &id(const int &x) { return x; }
const int c = 4;
constexpr int r = id(c);
static_assert(r == 4, "");
