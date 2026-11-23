// [namespace.udecl] Example 6 (paragraph 7), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P5
// status: annotated

struct X {
  int i;
  static int s;
};

void f() {
  using X::i;                   // error: X::i is a class member and this is not a member declaration. // semantic
  using X::s;                   // error: X::s is a class member and this is not a member declaration. // semantic
}
