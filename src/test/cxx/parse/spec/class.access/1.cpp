// [class.access] Example 1 (paragraph 3), docs/cxx-spec/class/class.access.md
// needs: P5
// status: well-formed (as far as the example says)

class X {
  int a;            // X::a is private by default
};

struct S {
  int a;            // S::a is public by default
};
