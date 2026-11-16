// [class.virtual] Example 4 (paragraph 5), docs/cxx-spec/class/class.virtual.md
// needs: P5
// status: annotated

struct B {
  virtual void f(int);
};

struct D : B {
  virtual void f(long) override;        // error: wrong signature overriding B::f // semantic
  virtual void f(int) override;         // OK
};
