// [dcl.init.aggr] Example 9 (paragraph 13), docs/cxx-spec/dcl/dcl.init.aggr.md
// needs: P5
// status: well-formed (as far as the example says)

struct S { } s;
struct A {
  S s1;
  int i1;
  S s2;
  int i2;
  S s3;
  int i3;
} a = {
  { },              // Required initialization
  0,
  s,                // Required initialization
  0
};                  // Initialization not required for A::s3 because A::i3 is also not initialized
