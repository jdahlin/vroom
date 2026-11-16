// [dcl.constexpr] Example 5 (paragraph 9), docs/cxx-spec/dcl/dcl.constexpr.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

constexpr int bar(int x, int y)         // OK
    { return x + y + x*y; }
// ...
int bar(int x, int y)                   // error: redefinition of bar
    { return x * 2 + 3 * y; }
