// [dcl.init.aggr] Example 5 (paragraph 9), docs/cxx-spec/dcl/dcl.init.aggr.md
// needs: P5
// status: annotated

struct S {
  int y[] = { 0 };          // error: non-static data member of incomplete type // semantic
};
