// [expr.prim.id.unqual] Example 1 (paragraph 2), docs/cxx-spec/expr/expr.prim.id.unqual.md
// needs: P6
// status: well-formed (as far as the example says)

void f() {
  float x, &r = x;
  [=] {
    decltype(x) y1;             // y1 has type float
    decltype((x)) y2 = y1;      // y2 has type float const& because this lambda
                                // is not mutable and x is an lvalue
    decltype(r) r1 = y1;        // r1 has type float&
    decltype((r)) r2 = y2;      // r2 has type float const&
  };
}
