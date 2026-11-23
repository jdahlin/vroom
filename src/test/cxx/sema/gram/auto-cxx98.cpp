// RUN: %clang_cc1 -std=c++98 -fsyntax-only %s
// [ext.clang]: before C++11 clang takes auto with no other type specifier as
// C++11's placeholder ([dcl.spec.auto]/1), with another as [dcl.stc]'s
// (C++03) storage class.
int zero;
void f() {
  auto x = 1;
  auto *p = &x;
  const auto c = 3;
  auto const d = 4;
  for (auto q = p; q != 0; q = 0) {
  }
  (void)c;
  (void)d;
}
