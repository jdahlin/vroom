// [dcl.typedef] Example 6 (paragraph 6), docs/cxx-spec/dcl/dcl.typedef.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

class complex { /* ... */ };
typedef int complex;            // error: redefinition
