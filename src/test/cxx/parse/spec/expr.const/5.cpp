// [expr.const] Example 5 (paragraph 11), docs/cxx-spec/expr/expr.const.md
// needs: P4
// status: annotated

consteval int f() { return 42; }
consteval auto g() { return f; }
consteval int h(int (*p)() = g()) { return p(); }
constexpr int r = h();                          // OK
constexpr auto e = g();                         // error: a pointer to an immediate function is // semantic
                                                // not a permitted result of a constant expression
