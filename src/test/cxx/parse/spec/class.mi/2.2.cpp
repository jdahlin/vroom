// [class.mi] Example 2, part 2 (paragraph 3), docs/cxx-spec/class/class.mi.md
// needs: P5
// status: well-formed (as far as the example says)

class L { public: int next;  /* ... */ };
class A : public L { /* ... */ };
class B : public L { /* ... */ };
class C : public A, public B { void f(); /* ... */ };   // well-formed
class D : public A, public L { void f(); /* ... */ };   // well-formed
