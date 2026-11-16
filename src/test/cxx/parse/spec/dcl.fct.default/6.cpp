// [dcl.fct.default] Example 6 (paragraph 8), docs/cxx-spec/dcl/dcl.fct.default.md
// needs: P5
// status: annotated

class A {
  void f(A* p = this) { }           // error // semantic
};
