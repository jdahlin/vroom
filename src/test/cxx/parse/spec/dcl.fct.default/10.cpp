// [dcl.fct.default] Example 10 (paragraph 10), docs/cxx-spec/dcl/dcl.fct.default.md
// needs: P5
// status: annotated

struct A {
  virtual void f(int a = 7);
};
struct B : public A {
  void f(int a);
};
void m() {
  B* pb = new B;
  A* pa = pb;
  pa->f();          // OK, calls pa->B::f(7)
  pb->f();          // error: wrong number of arguments for B::f() // semantic
}
