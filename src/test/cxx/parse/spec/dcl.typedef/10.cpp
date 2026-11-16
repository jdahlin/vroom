// [dcl.typedef] Example 10 (paragraph 10), docs/cxx-spec/dcl/dcl.typedef.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

typedef struct {
  int f() {}
} X;                            // error: struct with typedef name for linkage has member functions
