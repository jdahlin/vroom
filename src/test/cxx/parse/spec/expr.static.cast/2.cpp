// [expr.static.cast] Example 2 (paragraph 7), docs/cxx-spec/expr/expr.static.cast.md
// needs: P5
// status: annotated

struct B { };
struct D : private B { };
void f() {
  static_cast<D*>((B*)0);               // error: B is a private base of D // semantic
  static_cast<int B::*>((int D::*)0);   // error: B is a private base of D // semantic
}
