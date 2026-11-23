// [dcl.init.ref] Example 2 (paragraph 3), docs/cxx-spec/dcl/dcl.init.ref.md
// needs: P3
// status: annotated

int& r1;                        // error: initializer missing // semantic
extern int& r2;                 // OK
