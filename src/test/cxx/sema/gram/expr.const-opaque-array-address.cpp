// RUN: %clang_cc1 -std=c++17 -fsyntax-only -verify %s
// [expr.add]/4, [expr.rel]/4, [expr.const]/13.1: the address of an element
// of an array of static storage duration is a constant even where the
// values of its elements are not known (declared only), and pointers to its
// elements compare by index.
extern int x[2];
constexpr int *a = &x[0];
constexpr int *b = x + 1;
static_assert(a < b, "");
static_assert(&x[0] < &x[1], "");
static_assert(a != b, "");
static_assert(a == &x[0], "");
// expected-no-diagnostics
