// [dcl.spec] Example 1 (paragraph 3), docs/cxx-spec/dcl/dcl.spec.md
// needs: P5
// status: annotated

typedef char* Pc;
static Pc;                      // error: name missing // semantic
