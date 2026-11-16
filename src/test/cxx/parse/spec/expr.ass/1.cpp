// [expr.ass] Example 1 (paragraph 8), docs/cxx-spec/expr/expr.ass.md
// needs: P4
// status: annotated

complex<double> z;
z = { 1,2 };        // meaning z.operator=({1,2})
z += { 1, 2 };      // meaning z.operator+=({1,2})
int a, b;
a = b = { 1 };      // meaning a=b=1;
a = { 1 } = b;      // syntax error
