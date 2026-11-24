// [ext:gnu_attributes] require_constant_initialization: dynamic
// initialization is an error (clang: "variable does not have a constant
// initializer").
// RUN: %clang_cc1 -std=c++20 -fsyntax-only %s
int f();
__attribute__((require_constant_initialization)) int z = f();
