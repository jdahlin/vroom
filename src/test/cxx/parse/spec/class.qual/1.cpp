// [class.qual] Example 1 (paragraph 2), docs/cxx-spec/basic/class.qual.md
// needs: P5
// status: annotated

struct A { A(); };
struct B: public A { B(); };

A::A() { }
B::B() { }

B::A ba;            // object of type A
A::A a;             // error: A::A is not a type name // semantic
struct A::A a2;     // object of type A
