// RUN: %clang_cc1 -std=c++17 -fsyntax-only %s
// [diff.cpp14.except]/1: a dynamic-exception-specification of a member of a
// class template is an error of the template's definition, instantiated or not.
template <class T> struct X {
  void f() throw(T) {}
};
template <class T> struct Y {
  void f() throw(int);
};
