// [dcl.typedef] Example 10 (paragraph 10), docs/cxx-spec/dcl/dcl.typedef.md
// needs: P5
// status: annotated

typedef struct {
  int f() {}
} X;                            // error: struct with typedef name for linkage has member functions // semantic
