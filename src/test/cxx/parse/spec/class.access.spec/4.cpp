// [class.access.spec] Example 4 (paragraph 5), docs/cxx-spec/class/class.access.spec.md
// needs: P5
// status: annotated

class A { };
class B : private A { };
class C : public B {
  A* p;             // error: injected-class-name A is inaccessible // semantic
  ::A* q;           // OK
};
