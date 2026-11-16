// [dcl.ref] Example 1 (paragraph 1), docs/cxx-spec/dcl/dcl.ref.md
// needs: P5
// status: annotated

typedef int& A;
const A aref = 3;   // error: lvalue reference to non-const initialized with rvalue // semantic
