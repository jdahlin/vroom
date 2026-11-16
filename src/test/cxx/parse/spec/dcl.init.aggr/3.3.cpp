// [dcl.init.aggr] Example 3, part 3 (paragraph 5), docs/cxx-spec/dcl/dcl.init.aggr.md
// needs: P5
// status: well-formed (as far as the example says)

struct A {
  string a;
  int b = 42;
  int c = -1;
};
