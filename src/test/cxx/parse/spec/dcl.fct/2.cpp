// [dcl.fct] Example 2 (paragraph 6), docs/cxx-spec/dcl/dcl.fct.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

typedef int FIC(int) const;
FIC f;              // error: does not declare a member function
struct S {
  FIC f;            // OK
};
FIC S::*pm = &S::f; // OK
