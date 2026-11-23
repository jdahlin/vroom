// [namespace.udecl] Example 5 (paragraph 5), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P7
// status: annotated

struct A {
  template <class T> void f(T);
  template <class T> struct X { };
};
struct B : A {
  using A::f<double>;           // error // semantic
  using A::X<int>;              // error // semantic
};
