// RUN: %clang_cc1 -std=c++14 -fsyntax-only %s
// [except.spec]/1 (C++14), Microsoft's throw ( ... ) which clang parses in
// every edition: a function, a member function, a pointer to member
// function.
struct A {
  void f();
  void g() throw(...);
};
void h() throw(...);
void (A::*p)() throw(...) = &A::f;
void use() { h(); }
