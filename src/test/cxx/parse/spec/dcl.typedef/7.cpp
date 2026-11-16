// [dcl.typedef] Example 7 (paragraph 7), docs/cxx-spec/dcl/dcl.typedef.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

typedef int complex;
class complex { /* ... */ };    // error: redefinition
