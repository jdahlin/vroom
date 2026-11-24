// RUN: %clang_cc1 -std=c++17 -fsyntax-only -verify %s
// [expr.const]/13, [expr.eq]/3: the address of a function is a permitted
// result; two pointers to one function compare equal, to different ones
// unequal, and a function pointer is not equal to a null pointer.
void a();
void b();
static_assert(&a == &a, "");
static_assert(&a != &b, "");
static_assert(&a != nullptr, "");
static_assert(a == a, "");
static_assert(a != b, "");
constexpr void (*p)() = &a;
static_assert(p == &a, "");
static_assert(p != &b, "");
static_assert(p, "");
constexpr void (*f())() { void h(); return &h; }
constexpr void (*g())() { void h(); return &h; }
static_assert(f() == g(), "");
static_assert(&a == &b, ""); // expected-error {{static assertion failed}}
template <typename T> void tmpl(T) {}
static_assert(__builtin_addressof(tmpl<int>) == &tmpl<int>, ""); // expected-error {{reference to overloaded function could not be resolved}}
