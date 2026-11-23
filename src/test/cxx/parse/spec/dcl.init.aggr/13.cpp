// [dcl.init.aggr] Example 13 (paragraph 19), docs/cxx-spec/dcl/dcl.init.aggr.md
// needs: P5
// status: annotated

union u { int a; const char* b; };
u a = { 1 };
u b = a;
u c = 1;                        // error // semantic
u d = { 0, "asdf" };            // error // semantic
u e = { "asdf" };               // error // semantic
u f = { .b = "asdf" };
u g = { .a = 1, .b = "asdf" };  // error // semantic
