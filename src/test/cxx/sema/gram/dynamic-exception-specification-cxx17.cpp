// RUN: %clang_cc1 -std=c++17 -Wno-dynamic-exception-spec -fsyntax-only %s
// [diff.cpp14.except]/1: C++17 removed the dynamic-exception-specification;
// clang keeps it as an extension (an error unless
// -Wno-dynamic-exception-spec): the declaration is made, the specification
// parsed in a function, a pointer to member function, and a template.
struct A {
  void f();
  void g() throw(int);
};
void (A::*p)() throw(int) = &A::f;
void h() throw(int, char *);
template <class T> void t() throw(T);
void use() { h(); t<int>(); }
