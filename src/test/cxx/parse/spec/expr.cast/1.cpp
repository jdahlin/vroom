// [expr.cast] Example 1 (paragraph 4), docs/cxx-spec/expr/expr.cast.md
// needs: P5
// status: annotated

struct A { };
struct I1 : A { };
struct I2 : A { };
struct D : I1, I2 { };
A* foo( D* p ) {
  return (A*)( p );             // ill-formed static_cast interpretation // semantic
}
