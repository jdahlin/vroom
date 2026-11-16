// [class.virtual] Example 1 (paragraph 2), docs/cxx-spec/class/class.virtual.md
// needs: P5
// status: well-formed (as far as the example says)

struct A {
  virtual void f();
};
struct B : virtual A {
  virtual void f();
};
struct C : B , virtual A {
  using A::f;
};

void foo() {
  C c;
  c.f();            // calls B::f, the final overrider
  c.C::f();         // calls A::f because of the using-declaration
}
