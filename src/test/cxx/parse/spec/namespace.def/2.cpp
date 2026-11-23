// [namespace.def] Example 2 (paragraph 4), docs/cxx-spec/dcl/namespace.def.md
// needs: P5
// status: well-formed (as far as the example says)

namespace Q {
  namespace V {
    void f();                   // enclosing namespaces are the global namespace, Q, and Q::V
    class C { void m(); };
  }
  void V::f() {                 // enclosing namespaces are the global namespace, Q, and Q::V
    extern void h();            // ... so this declares Q::V::h
  }
  void V::C::m() {              // enclosing namespaces are the global namespace, Q, and Q::V
  }
}
