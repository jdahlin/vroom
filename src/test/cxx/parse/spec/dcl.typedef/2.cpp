// [dcl.typedef] Example 2 (paragraph 2), docs/cxx-spec/dcl/dcl.typedef.md
// needs: P7
// status: annotated

using handler_t = void (*)(int);
extern handler_t ignore;
extern void (*ignore)(int);         // redeclare ignore
using cell = pair<void*, cell*>;    // error // syntax-error
