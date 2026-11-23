// [namespace.memdef] Example 1 (paragraph 1), docs/cxx-spec/dcl/namespace.memdef.md
// needs: P5
// status: annotated

namespace X {
  void f() { /* ... */ }        // OK: introduces X::f()

  namespace M {
    void g();                   // OK: introduces X::M::g()
  }
  using M::g;
  void g();                     // error: conflicts with X::M::g() // semantic
}
