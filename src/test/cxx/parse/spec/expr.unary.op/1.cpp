// [expr.unary.op] Example 1 (paragraph 3), docs/cxx-spec/expr/expr.unary.op.md
// needs: P7
// status: well-formed (as far as the example says)

struct A { int i; };
struct B : A { };
... &B::i ...       // has type int A::*
int a;
int* p1 = &a;
int* p2 = p1 + 1;   // defined behavior
bool b = p2 > p1;   // defined behavior, with value true
