// [expr.prim.lambda] Example 3 (paragraph 5), docs/cxx-spec/expr/expr.prim.lambda.md
// needs: P7 (the generic lambdas have template-parameter-lists)
// status: well-formed (as far as the example says)

int i = [](int i, auto a) { return i; }(3, 4);                  // OK: a generic lambda
int j = []<class T>(T t, int i) { return i; }(3, 4);            // OK: a generic lambda
