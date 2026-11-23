// [namespace.udecl] Example 8 (paragraph 9), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P5
// status: annotated

namespace A {
  int i;
}

namespace A1 {
  using A::i, A::i;             // OK: double declaration
}

struct B {
  int i;
};

struct X : B {
  using B::i, B::i;             // error: double member declaration // semantic
};
