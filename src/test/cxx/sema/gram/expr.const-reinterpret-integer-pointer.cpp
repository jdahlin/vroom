// RUN: %clang_cc1 -std=c++17 -fsyntax-only -verify %s
// [expr.const]/5.15 (n4861): a reinterpret_cast is not a core constant
// expression; an integer converted to a pointer is one ([expr.reinterpret.cast]/5),
// whatever its value, except in a __builtin_constant_p condition's arm,
// which is always a potential constant.
int g;
constexpr bool a(int n) { return (int *)n != &g; } // expected-warning {{cast to}} expected-error {{constexpr function never produces a constant expression}} expected-note {{cast that performs the conversions of a reinterpret_cast is not allowed in a constant expression}}
constexpr bool d(int n)
{
    return __builtin_constant_p((int *)n != &g) ? (int *)n != &g : (int *)n != &g; // expected-warning 3 {{cast to 'int *' from smaller integer type 'int'}}
}
constexpr int *f = (int *)0;
constexpr int *h = (int *)8; // expected-error {{constexpr variable 'h' must be initialized by a constant expression}} expected-note {{cast that performs the conversions of a reinterpret_cast is not allowed in a constant expression}}
