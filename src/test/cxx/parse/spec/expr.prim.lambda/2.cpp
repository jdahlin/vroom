// [expr.prim.lambda] Example 2 (paragraph 4), docs/cxx-spec/expr/expr.prim.lambda.md
// needs: P6
// status: annotated

auto x1 = [](int i){ return i; };       // OK: return type is int
auto x2 = []{ return { 1, 2 }; };       // error: deducing return type from braced-init-list // semantic
int j;
auto x3 = []()->auto&& { return j; };   // OK: return type is int&
