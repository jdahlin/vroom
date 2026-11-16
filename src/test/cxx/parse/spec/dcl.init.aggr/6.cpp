// [dcl.init.aggr] Example 6 (paragraph 10), docs/cxx-spec/dcl/dcl.init.aggr.md
// needs: P5
// status: well-formed (as far as the example says)

struct A {
  int i;
  static int s;
  int j;
  int :17;
  int k;
} a = { 1, 2, 3 };
