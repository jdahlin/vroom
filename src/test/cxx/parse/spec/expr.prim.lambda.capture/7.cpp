// [expr.prim.lambda.capture] Example 7 (paragraph 9), docs/cxx-spec/expr/expr.prim.lambda.capture.md
// needs: P6
// status: annotated

void f2() {
  int i = 1;
  void g1(int = ([i]{ return i; })());          // error // semantic
  void g2(int = ([i]{ return 0; })());          // error // semantic
  void g3(int = ([=]{ return i; })());          // error // semantic
  void g4(int = ([=]{ return 0; })());          // OK
  void g5(int = ([]{ return sizeof i; })());    // OK
  void g6(int = ([x=1]{ return x; })());        // OK
  void g7(int = ([x=i]{ return x; })());        // error // semantic
}
