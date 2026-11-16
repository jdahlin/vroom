// [class.copy.assign] Example 2 (paragraph 4), docs/cxx-spec/class/class.copy.assign.md
// needs: P5
// status: well-formed (as far as the example says)

struct S {
  int a;
  S& operator=(const S&) = default;
};
