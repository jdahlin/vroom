// [expr.prim.lambda.closure] Example 2 (paragraph 4), docs/cxx-spec/expr/expr.prim.lambda.closure.md
// needs: P6
// status: annotated

auto ID = [](auto a) { return a; };
static_assert(ID(3) == 3);                      // OK

struct NonLiteral {
  NonLiteral(int n) : n(n) { }
  int n;
};
static_assert(ID(NonLiteral{3}).n == 3);        // error // semantic
