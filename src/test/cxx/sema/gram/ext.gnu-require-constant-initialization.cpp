// [ext:gnu_attributes] require_constant_initialization: constinit's rule
// for a variable with static storage duration.
// RUN: %clang_cc1 -std=c++20 -fsyntax-only %s
constexpr int one() { return 1; }
__attribute__((require_constant_initialization)) int a = one();
__attribute__((require_constant_initialization)) int b = 3;
int c __attribute__((require_constant_initialization)) = one() + 2;
static __attribute__((require_constant_initialization)) int d = 4;
void g() {
  static __attribute__((require_constant_initialization)) int e = one();
}
