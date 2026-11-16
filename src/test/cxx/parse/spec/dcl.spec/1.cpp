// [dcl.spec] Example 1 (paragraph 3), docs/cxx-spec/dcl/dcl.spec.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

typedef char* Pc;
static Pc;                      // error: name missing
