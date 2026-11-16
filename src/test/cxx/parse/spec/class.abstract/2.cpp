// [class.abstract] Example 2 (paragraph 2), docs/cxx-spec/class/class.abstract.md
// needs: P5
// status: annotated

struct C {
  virtual void f() = 0 { };     // error // syntax-error
};
