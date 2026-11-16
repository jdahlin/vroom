// [dcl.fct] Example 2 (paragraph 6), docs/cxx-spec/dcl/dcl.fct.md
// needs: P5
// status: annotated

typedef int FIC(int) const;
FIC f;              // error: does not declare a member function // semantic
struct S {
  FIC f;            // OK
};
FIC S::*pm = &S::f; // OK
