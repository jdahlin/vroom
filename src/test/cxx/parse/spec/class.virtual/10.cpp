// [class.virtual] Example 10 (paragraph 16), docs/cxx-spec/class/class.virtual.md
// needs: P5
// status: well-formed (as far as the example says)

class B { public: virtual void f(); };
class D : public B { public: void f(); };

void D::f() { /* ... */ B::f(); }
