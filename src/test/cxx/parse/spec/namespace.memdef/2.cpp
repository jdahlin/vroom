// [namespace.memdef] Example 2 (paragraph 2), docs/cxx-spec/dcl/namespace.memdef.md
// needs: P5
// status: annotated

namespace Q {
  namespace V {
    void f();
  }
  void V::f() { /* ... */ }     // OK
  void V::g() { /* ... */ }     // error: g() is not yet a member of V // semantic
  namespace V {
    void g();
  }
}

namespace R {
  void Q::V::g() { /* ... */ }  // error: R doesn't enclose Q // semantic
}
