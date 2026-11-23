// [class.base.init] Example 2 (paragraph 4), docs/cxx-spec/class/class.base.init.md
// needs: P5
// status: annotated

struct A { A(); };
struct B: public virtual A { };
struct C: public A, public B { C(); };
C::C(): A() { }                 // error: which A? // semantic
