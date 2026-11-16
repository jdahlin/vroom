// [dcl.ref] Example 1 (paragraph 1), docs/cxx-spec/dcl/dcl.ref.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

typedef int& A;
const A aref = 3;   // error: lvalue reference to non-const initialized with rvalue
