// [expr.prim.id.dtor] Example 1 (paragraph 3), docs/cxx-spec/expr/expr.prim.id.dtor.md
// needs: P5
// status: annotated

struct C { };
void f() {
  C * pc = new C;
  using C2 = C;
  pc->C::~C2();     // OK, destroys *pc
  C().C::~C();      // undefined behavior: temporary of type C destroyed twice
  using T = int;
  0 .T::~T();       // OK, no effect
  0.T::~T();        // error: 0.T is a user-defined-floating-point-literal ([lex.ext]) // syntax-error
}
