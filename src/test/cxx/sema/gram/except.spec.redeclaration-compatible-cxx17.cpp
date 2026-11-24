// RUN: %clang_cc1 -std=c++17 -fsyntax-only -Wno-dynamic-exception-spec %s
// [except.spec]/4 in C++17, where a dynamic specification is deprecated: the
// same rule, a mismatch an error of the later declaration.
void g() throw(int);
void g() throw(char);
template <typename A, typename B> void t() throw(A);
template <typename A, typename B> void t() throw(B);
