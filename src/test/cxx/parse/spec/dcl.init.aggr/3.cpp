// [dcl.init.aggr] Example 3 (paragraph 5), docs/cxx-spec/dcl/dcl.init.aggr.md
// needs: P5
// status: well-formed (as far as the example says)

struct S { int a; const char* b; int c; int d = b[a]; };
S ss = { 1, "asdf" };
