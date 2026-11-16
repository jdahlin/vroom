// [dcl.type.decltype] Example 1 (paragraph 1), docs/cxx-spec/dcl/dcl.type.decltype.md
// needs: P5
// status: well-formed (as far as the example says)

const int&& foo();
int i;
struct A { double x; };
const A* a = new A();
decltype(foo()) x1 = 17;        // type is const int&&
decltype(i) x2;                 // type is int
decltype(a->x) x3;              // type is double
decltype((a->x)) x4 = x3;       // type is const double&
