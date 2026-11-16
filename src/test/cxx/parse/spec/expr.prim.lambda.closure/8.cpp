// [expr.prim.lambda.closure] Example 8 (paragraph 12), docs/cxx-spec/expr/expr.prim.lambda.closure.md
// needs: P6
// status: well-formed (as far as the example says)

struct S1 {
  int x, y;
  int operator()(int);
  void f() {
    [=]()->int {
      return operator()(this->x + y);   // equivalent to S1::operator()(this->x + (*this).y)
                                        // this has type S1*
    };
  }
};
