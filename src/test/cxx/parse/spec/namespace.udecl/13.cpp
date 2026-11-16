// [namespace.udecl] Example 13 (paragraph 18), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P5
// status: annotated

struct A { int x(); };
struct B : A { };
struct C : A {
  using A::x;
  int x(int);
};

struct D : B, C {
  using C::x;
  int x(double);
};
int f(D* d) {
  return d->x();    // error: overload resolution selects A::x, but A is an ambiguous base class // semantic
}
