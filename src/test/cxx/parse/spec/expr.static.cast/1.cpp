// [expr.static.cast] Example 1 (paragraph 2), docs/cxx-spec/expr/expr.static.cast.md
// needs: P5
// status: well-formed (as far as the example says)

struct B { };
struct D : public B { };
D d;
B &br = d;

static_cast<D&>(br);            // produces lvalue denoting the original d object
