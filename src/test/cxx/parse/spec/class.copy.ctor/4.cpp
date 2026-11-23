// [class.copy.ctor] Example 4 (paragraph 4), docs/cxx-spec/class/class.copy.ctor.md
// needs: P5
// status: annotated

struct X {
  X();              // default constructor
  X(X&);            // copy constructor with a non-const parameter
};
const X cx;
X x = cx;           // error: X::X(X&) cannot copy cx into x // semantic
