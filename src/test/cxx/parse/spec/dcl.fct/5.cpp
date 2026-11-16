// [dcl.fct] Example 5 (paragraph 14), docs/cxx-spec/dcl/dcl.fct.md
// needs: P5
// status: annotated

typedef void F();
F  fv;              // OK: equivalent to void fv();
F  fv { }           // error // syntax-error
void fv() { }       // OK: definition of fv
