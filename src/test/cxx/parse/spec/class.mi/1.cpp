// [class.mi] Example 1 (paragraph 1), docs/cxx-spec/class/class.mi.md
// needs: P5
// status: well-formed (as far as the example says)

class A { /* ... */ };
class B { /* ... */ };
class C { /* ... */ };
class D : public A, public B, public C { /* ... */ };
