// [dcl.enum] Example 2 (paragraph 2), docs/cxx-spec/dcl/dcl.enum.md
// needs: P3
// status: annotated

enum { a, b, c=0 };
enum { d, e, f=e+2 };
