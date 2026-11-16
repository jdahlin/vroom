// [dcl.fct.default] Example 8 (paragraph 9), docs/cxx-spec/dcl/dcl.fct.default.md
// needs: P5
// status: annotated

int b;
class X {
  int a;
  int mem1(int i = a);              // error: non-static member a used as default argument // semantic
  int mem2(int i = b);              // OK; use X::b
  static int b;
};
