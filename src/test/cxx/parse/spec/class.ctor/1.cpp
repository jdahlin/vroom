// [class.ctor] Example 1 (paragraph 1), docs/cxx-spec/class/class.ctor.md
// needs: P5
// status: well-formed (as far as the example says)

struct S {
  S();              // declares the constructor
};

S::S() { }          // defines the constructor
