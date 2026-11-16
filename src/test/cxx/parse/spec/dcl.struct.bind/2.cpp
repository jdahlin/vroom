// [dcl.struct.bind] Example 2 (paragraph 5), docs/cxx-spec/dcl/dcl.struct.bind.md
// needs: P5
// status: well-formed (as far as the example says)

struct S { int x1 : 2; volatile double y1; };
S f();
const auto [ x, y ] = f();
