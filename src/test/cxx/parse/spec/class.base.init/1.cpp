// [class.base.init] Example 1 (paragraph 3), docs/cxx-spec/class/class.base.init.md
// needs: P5
// status: well-formed (as far as the example says)

struct A { A(); };
typedef A global_A;
struct B { };
struct C: public A, public B { C(); };
C::C(): global_A() { }          // mem-initializer for base A
