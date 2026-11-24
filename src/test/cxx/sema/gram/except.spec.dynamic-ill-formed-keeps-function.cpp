// RUN: %clang_cc1 -std=c++17 -fsyntax-only %s
// [diff.cpp14.except]/1: a dynamic-exception-specification is an error in
// C++17, after which clang goes on: the function is declared, later
// declarations and uses are analysed.
void f(int) throw(int);
void f(char *);
void g() { f(1); f("x"); }
