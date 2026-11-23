// [class.mem] Example 4 (paragraph 22), docs/cxx-spec/class/class.mem.md
// needs: P5
// status: well-formed (as far as the example says)

struct A { int a; char b; };
struct B { const int b1; volatile char b2; };
struct C { int c; unsigned : 0; char b; };
struct D { int d; char b : 4; };
struct E { unsigned int e; char b; };
