// [dcl.name] Example 1 (paragraph 1), docs/cxx-spec/dcl/dcl.name.md
// needs: P5
// status: not a translation unit (type-ids, one per line): a syntax error is expected at its end

int                 // int i
int *               // int *pi
int *[3]            // int *p[3]
int (*)[3]          // int (*p3i)[3]
int *()             // int *f()
int (*)(double)     // int (*pf)(double)
