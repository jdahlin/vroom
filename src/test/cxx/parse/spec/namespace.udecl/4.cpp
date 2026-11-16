// [namespace.udecl] Example 4 (paragraph 3), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P5
// status: annotated

class C {
  int g();
};

class D2 : public B {
  using B::f;                   // OK: B is a base of D2
  using B::e;                   // OK: e is an enumerator of base B
  using B::x;                   // OK: x is a union member of base B
  using C::g;                   // error: C isn't a base of D2 // semantic
};
