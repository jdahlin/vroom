// [expr.const] Example 3 (paragraph 5), docs/cxx-spec/expr/expr.const.md
// needs: P5
// status: annotated

int x;                              // not constant
struct A {
  constexpr A(bool b) : m(b?42:x) { }
  int m;
};
constexpr int v = A(true).m;        // OK: constructor call initializes m with the value 42

constexpr int w = A(false).m;       // error: initializer for m is x, which is non-constant // semantic

constexpr int f1(int k) {
  constexpr int x = k;              // error: x is not initialized by a constant expression // semantic
                                    // because lifetime of k began outside the initializer of x
  return x;
}
constexpr int f2(int k) {
  int x = k;                        // OK: not required to be a constant expression
                                    // because x is not constexpr
  return x;
}

constexpr int incr(int &n) {
  return ++n;
}
constexpr int g(int k) {
  constexpr int x = incr(k);        // error: incr(k) is not a core constant expression // semantic
                                    // because lifetime of k began outside the expression incr(k)
  return x;
}
constexpr int h(int k) {
  int x = incr(k);                  // OK: incr(k) is not required to be a core constant expression
  return x;
}
constexpr int y = h(1);             // OK: initializes y with the value 2
                                    // h(1) is a core constant expression because
                                    // the lifetime of k begins inside h(1)
