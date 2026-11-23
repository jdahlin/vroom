// [dcl.typedef] Example 6 (paragraph 6), docs/cxx-spec/dcl/dcl.typedef.md
// needs: P5
// status: annotated

class complex { /* ... */ };
typedef int complex;            // error: redefinition // semantic
