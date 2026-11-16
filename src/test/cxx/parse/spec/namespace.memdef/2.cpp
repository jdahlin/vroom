// [namespace.memdef] Example 2 (paragraph 2), docs/cxx-spec/dcl/namespace.memdef.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

namespace Q {
  namespace V {
    void f();
  }
  void V::f() { /* ... */ }     // OK
  void V::g() { /* ... */ }     // error: g() is not yet a member of V
  namespace V {
    void g();
  }
}

namespace R {
  void Q::V::g() { /* ... */ }  // error: R doesn't enclose Q
}
