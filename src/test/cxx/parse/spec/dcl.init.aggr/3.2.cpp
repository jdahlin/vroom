// [dcl.init.aggr] Example 3, part 2 (paragraph 5), docs/cxx-spec/dcl/dcl.init.aggr.md
// needs: P5
// status: well-formed (as far as the example says)

struct X { int i, j, k = 42; };
X a[] = { 1, 2, 3, 4, 5, 6 };
X b[2] = { { 1, 2, 3 }, { 4, 5, 6 } };
