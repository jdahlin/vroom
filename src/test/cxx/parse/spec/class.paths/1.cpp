// [class.paths] Example 1 (paragraph 1), docs/cxx-spec/class/class.paths.md
// needs: P5
// status: well-formed (as far as the example says)

class W { public: void f(); };
class A : private virtual W { };
class B : public virtual W { };
class C : public A, public B {
  void f() { W::f(); }          // OK
};
