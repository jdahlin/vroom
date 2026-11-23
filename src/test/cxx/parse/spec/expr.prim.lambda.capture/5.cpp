// [expr.prim.lambda.capture] Example 5 (paragraph 7), docs/cxx-spec/expr/expr.prim.lambda.capture.md
// needs: P7
// status: well-formed (as far as the example says)

template<bool B>
void f(int n) {
  [=](auto a) {
    if constexpr (B && sizeof(a) > 4) {
      (void)n;                  // captures n regardless of the value of B and sizeof(int)
    }
  }(0);
}
