// [namespace.udecl] Example 11 (paragraph 13), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P4
// status: annotated

namespace B {
  void f(int);
  void f(double);
}
namespace C {
  void f(int);
  void f(double);
  void f(char);
}

void h() {
  using B::f;       // B::f(int) and B::f(double)
  using C::f;       // C::f(int), C::f(double), and C::f(char)
  f('h');           // calls C::f(char)
  f(1);             // error: ambiguous: B::f(int) or C::f(int)? // semantic
  void f(int);      // error: f(int) conflicts with C::f(int) and B::f(int) // semantic
}
