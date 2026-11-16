// [dcl.constexpr] Example 6 (paragraph 10), docs/cxx-spec/dcl/dcl.constexpr.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

struct pixel {
  int x, y;
};
constexpr pixel ur = { 1294, 1024 };    // OK
constexpr pixel origin;                 // error: initializer missing
