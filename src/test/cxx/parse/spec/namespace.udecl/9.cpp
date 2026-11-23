// [namespace.udecl] Example 9 (paragraph 10), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P4
// status: annotated

namespace A {
  void f(int);
}

using A::f;         // f is a synonym for A::f; that is, for A::f(int).
namespace A {
  void f(char);
}

void foo() {
  f('a');           // calls f(int), even though f(char) exists.
}

void bar() {
  using A::f;       // f is a synonym for A::f; that is, for A::f(int) and A::f(char).
  f('a');           // calls f(char)
}
