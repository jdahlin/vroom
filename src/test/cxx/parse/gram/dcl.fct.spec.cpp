// RUN: %clang_cc1 -std=c++11 %s
// RUN: %clang_cc1 -std=c++17 %s
// RUN: %clang_cc1 -std=c++20 %s
// [dcl.fct.spec]/2: explicit ( constant-expression ), P0892R2 (C++20).  Before
// C++20 clang takes it as an extension with a warning
// ([ext:clang_later_edition_features]), and libc++'s <__utility/pair.h> uses it
// in every mode; the parse is the same in each.
struct S {
  explicit(true) S(int);
  explicit(false) S(char, int);
  explicit(sizeof(int) > 2) S(long, int);
  explicit S(short);
};
template <class T> struct P {
  template <class U> explicit(sizeof(T) > sizeof(U)) P(U);
  explicit(!__is_trivially_copyable(T)) P(const P &);
  explicit(false) operator bool() const;
};
