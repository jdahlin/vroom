// RUN: %clang_cc1 -std=c++23 %s
// P1102R2 (C++23): the parentheses of a lambda-declarator are optional when
// there is no parameter-declaration-clause: lambda-specifier-seq,
// noexcept-specifier and trailing-return-type may follow the introducer.
// P1169R4 (C++23): static is a lambda-specifier.  The parse keeps the
// specifiers as tokens of the lambda-declarator.
void f(int y) {
  auto a = [] constexpr -> int { return 1; };
  auto b = [] -> int { return 1; };
  auto c = [y] mutable noexcept { return y; };
  auto d = [&] noexcept(true) -> int { return y; };
  auto e = [] static { return 0; };
  auto g = [](int) static constexpr { return 0; };
  auto h = [] mutable { return 0; };
  (void)a; (void)b; (void)c; (void)d; (void)e; (void)g; (void)h;
}
