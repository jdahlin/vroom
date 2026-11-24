// RUN: %clang_cc1 -std=c++14 -fsyntax-only %s
// [except.spec]/4: all declarations of a function have a compatible exception
// specification: the same set of types, or both allow every exception; the
// function types do not say so before C++17, nor for a dynamic specification.
void g() throw(int);                 // 
void g() throw(char);                // 
void h() throw(int, char);
void h() throw(char, const int);     // same set
void k();
void k() throw(...);                 // both allow every exception
void m() noexcept(true);             // 
void m() noexcept(false);            // 
template <typename A, typename B> void t() throw(A); // 
template <typename A, typename B> void t() throw(B); // 
