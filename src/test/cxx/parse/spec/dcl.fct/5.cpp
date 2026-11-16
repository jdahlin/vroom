// [dcl.fct] Example 5 (paragraph 14), docs/cxx-spec/dcl/dcl.fct.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

typedef void F();
F  fv;              // OK: equivalent to void fv();
F  fv { }           // error
void fv() { }       // OK: definition of fv
