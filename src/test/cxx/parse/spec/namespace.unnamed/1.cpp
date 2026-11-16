// [namespace.unnamed] Example 1 (paragraph 1), docs/cxx-spec/dcl/namespace.unnamed.md
// needs: P5
// status: annotated

namespace { int i; }            // unique::i
void f() { i++; }               // unique::i++

namespace A {
  namespace {
    int i;                      // A::unique::i
    int j;                      // A::unique::j
  }
  void g() { i++; }             // A::unique::i++
}

using namespace A;
void h() {
  i++;                          // error: unique::i or A::unique::i // semantic
  A::i++;                       // A::unique::i
  j++;                          // A::unique::j
}
