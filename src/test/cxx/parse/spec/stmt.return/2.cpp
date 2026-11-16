// [stmt.return] Example 2 (paragraph 2), docs/cxx-spec/stmt/stmt.return.md
// needs: P5
// status: annotated

class A {
  ~A() {}
};
A f() { return A(); }   // error: destructor of A is private (even though it is never invoked) // semantic
