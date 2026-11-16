// [expr.prim.this] Example 2 (paragraph 4), docs/cxx-spec/expr/expr.prim.this.md
// needs: P5
// status: annotated

class Outer {
  int a[sizeof(*this)];                 // error: not inside a member function // semantic
  unsigned int sz = sizeof(*this);      // OK: in default member initializer

  void f() {
    int b[sizeof(*this)];               // OK

    struct Inner {
      int c[sizeof(*this)];             // error: not inside a member function of Inner // semantic
    };
  }
};
