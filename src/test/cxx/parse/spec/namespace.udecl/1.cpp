// [namespace.udecl] Example 1 (paragraph 2), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P5
// status: annotated

struct B {
  void f(char);
  void g(char);
  enum E { e };
  union { int x; };
};

struct D : B {
  using B::f;
  void f(int) { f('c'); }       // calls B::f(char)
  void g(int) { g('c'); }       // recursively calls D::g(int)
};
