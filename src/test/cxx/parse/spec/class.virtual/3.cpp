// [class.virtual] Example 3 (paragraph 4), docs/cxx-spec/class/class.virtual.md
// needs: P5
// status: annotated

struct B {
  virtual void f() const final;
};

struct D : B {
  void f() const;   // error: D::f attempts to override final B::f // semantic
};
