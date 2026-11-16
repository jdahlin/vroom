// [class.access.virt] Example 1 (paragraph 1), docs/cxx-spec/class/class.access.virt.md
// needs: P5
// status: annotated

class B {
public:
  virtual int f();
};

class D : public B {
private:
  int f();
};

void f() {
  D d;
  B* pb = &d;
  D* pd = &d;

  pb->f();                      // OK: B::f() is public, D::f() is invoked
  pd->f();                      // error: D::f() is private // semantic
}
