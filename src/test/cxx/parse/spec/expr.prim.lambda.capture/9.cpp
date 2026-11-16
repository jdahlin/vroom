// [expr.prim.lambda.capture] Example 9 (paragraph 12), docs/cxx-spec/expr/expr.prim.lambda.capture.md
// needs: P6
// status: well-formed (as far as the example says)

// The inner closure type must be a literal type regardless of how reference captures are represented.
static_assert([](int n) { return [&n] { return ++n; }(); }(3) == 4);
