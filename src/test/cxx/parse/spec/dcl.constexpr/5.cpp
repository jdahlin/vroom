// [dcl.constexpr] Example 5 (paragraph 9), docs/cxx-spec/dcl/dcl.constexpr.md
// needs: P5
// status: annotated

constexpr int bar(int x, int y)         // OK
    { return x + y + x*y; }
// ...
int bar(int x, int y)                   // error: redefinition of bar // semantic
    { return x * 2 + 3 * y; }
