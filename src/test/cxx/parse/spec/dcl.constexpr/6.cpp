// [dcl.constexpr] Example 6 (paragraph 10), docs/cxx-spec/dcl/dcl.constexpr.md
// needs: P5
// status: annotated

struct pixel {
  int x, y;
};
constexpr pixel ur = { 1294, 1024 };    // OK
constexpr pixel origin;                 // error: initializer missing // semantic
