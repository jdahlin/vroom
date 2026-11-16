// [namespace.udecl] Example 7 (paragraph 8), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P4
// status: annotated

void f();

namespace A {
  void g();
}

namespace X {
  using ::f;                    // global f
  using A::g;                   // A's g
}

void h()
{
  X::f();                       // calls ::f
  X::g();                       // calls A::g
}
