// [namespace.udir] Example 1 (paragraph 3), docs/cxx-spec/dcl/namespace.udir.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

namespace A {
  int i;
  namespace B {
    namespace C {
      int i;
    }
    using namespace A::B::C;
    void f1() {
      i = 5;        // OK, C::i visible in B and hides A::i
    }
  }
  namespace D {
    using namespace B;
    using namespace C;
    void f2() {
      i = 5;        // ambiguous, B::C::i or A::i?
    }
  }
  void f3() {
    i = 5;          // uses A::i
  }
}
void f4() {
  i = 5;            // error: neither i is visible
}
