// [expr.prim.lambda.capture] Example 2 (paragraph 5), docs/cxx-spec/expr/expr.prim.lambda.capture.md
// needs: P6
// status: annotated

void f() {
  int x = 0;
  auto g = [x](int x) { return 0; };    // error: parameter and simple-capture have the same name // semantic
}
