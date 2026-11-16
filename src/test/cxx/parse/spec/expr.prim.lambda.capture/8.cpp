// [expr.prim.lambda.capture] Example 8 (paragraph 11), docs/cxx-spec/expr/expr.prim.lambda.capture.md
// needs: P6
// status: well-formed (as far as the example says)

void f(const int*);
void g() {
  const int N = 10;
  [=] {
    int arr[N];     // OK: not an odr-use, refers to automatic variable
    f(&N);          // OK: causes N to be captured; &N points to
                    // the corresponding member of the closure type
  };
}
