// [dcl.typedef] Example 7 (paragraph 7), docs/cxx-spec/dcl/dcl.typedef.md
// needs: P5
// status: annotated

typedef int complex;
class complex { /* ... */ };    // error: redefinition // semantic
