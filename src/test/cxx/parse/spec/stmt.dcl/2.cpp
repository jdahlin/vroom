// [stmt.dcl] Example 2 (paragraph 4), docs/cxx-spec/stmt/stmt.dcl.md
// needs: P4
// status: well-formed (as far as the example says)

int foo(int i) {
  static int s = foo(2*i);      // undefined behavior: recursive call
  return i+1;
}
