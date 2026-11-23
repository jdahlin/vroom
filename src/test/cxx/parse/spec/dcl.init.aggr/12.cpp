// [dcl.init.aggr] Example 12 (paragraph 16), docs/cxx-spec/dcl/dcl.init.aggr.md
// needs: P5
// status: well-formed (as far as the example says)

struct A {
  int i;
  operator int();
};
struct B {
  A a1, a2;
  int z;
};
A a;
B b = { 4, a, a };
