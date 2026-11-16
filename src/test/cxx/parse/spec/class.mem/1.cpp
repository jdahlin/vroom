// [class.mem] Example 1 (paragraph 8), docs/cxx-spec/class/class.mem.md
// needs: P5
// status: well-formed (as far as the example says)

struct S {
  using T = void();
  T * p = 0;        // OK: brace-or-equal-initializer
  virtual T f = 0;  // OK: pure-specifier
};
