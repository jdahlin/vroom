// [namespace.def] Example 1 (paragraph 3), docs/cxx-spec/dcl/namespace.def.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

namespace Outer {
  int i;
  namespace Inner {
    void f() { i++; }           // Outer::i
    int i;
    void g() { i++; }           // Inner::i
  }
}
