// [except.spec] Example 1 (paragraph 4), docs/cxx-spec/except/except.spec.md
// needs: P6
// status: annotated

struct B {
  virtual void f() noexcept;
  virtual void g();
  virtual void h() noexcept = delete;
};

struct D: B {
  void f();                     // error // semantic
  void g() noexcept;            // OK
  void h() = delete;            // OK
};
