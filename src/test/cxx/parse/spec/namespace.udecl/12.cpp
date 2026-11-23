// [namespace.udecl] Example 12 (paragraph 14), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P5
// status: annotated

struct B {
  virtual void f(int);
  virtual void f(char);
  void g(int);
  void h(int);
};

struct D : B {
  using B::f;
  void f(int);      // OK: D::f(int) overrides B::f(int);

  using B::g;
  void g(char);     // OK

  using B::h;
  void h(int);      // OK: D::h(int) hides B::h(int)
};

void k(D* p)
{
  p->f(1);          // calls D::f(int)
  p->f('a');        // calls B::f(char)
  p->g(1);          // calls B::g(int)
  p->g('a');        // calls D::g(char)
}

struct B1 {
  B1(int);
};

struct B2 {
  B2(int);
};

struct D1 : B1, B2 {
  using B1::B1;
  using B2::B2;
};
D1 d1(0);           // error: ambiguous // semantic

struct D2 : B1, B2 {
  using B1::B1;
  using B2::B2;
  D2(int);          // OK: D2::D2(int) hides B1::B1(int) and B2::B2(int)
};
D2 d2(0);           // calls D2::D2(int)
