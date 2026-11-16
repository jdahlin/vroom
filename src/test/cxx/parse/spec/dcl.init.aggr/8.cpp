// [dcl.init.aggr] Example 8 (paragraph 12), docs/cxx-spec/dcl/dcl.init.aggr.md
// needs: P5
// status: annotated

struct A;
extern A a;
struct A {
  const A& a1 { A{a,a} };       // OK
  const A& a2 { A{} };          // error // semantic
};
A a{a,a};                       // OK

struct B {
  int n = B{}.n;                // error // semantic
};
