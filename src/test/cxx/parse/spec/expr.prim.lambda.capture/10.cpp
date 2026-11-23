// [expr.prim.lambda.capture] Example 10 (paragraph 13), docs/cxx-spec/expr/expr.prim.lambda.capture.md
// needs: P6
// status: well-formed (as far as the example says)

auto h(int &r) {
  return [&] {
    ++r;            // Valid after h returns if the lifetime of the
                    // object to which r is bound has not ended
  };
}
