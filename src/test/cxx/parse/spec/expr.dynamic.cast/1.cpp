// [expr.dynamic.cast] Example 1 (paragraph 4), docs/cxx-spec/expr/expr.dynamic.cast.md
// needs: P5
// status: well-formed (as far as the example says)

struct B { };
struct D : B { };
void foo(D* dp) {
  B*  bp = dynamic_cast<B*>(dp);    // equivalent to B* bp = dp;
}
