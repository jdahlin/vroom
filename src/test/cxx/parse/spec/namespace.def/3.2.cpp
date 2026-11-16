// [namespace.def] Example 3, part 2 (paragraph 9), docs/cxx-spec/dcl/namespace.def.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

namespace A {
  inline namespace B {
    namespace C {
      int i;
    }
  }
}
