// [class.virtual] Example 5 (paragraph 6), docs/cxx-spec/class/class.virtual.md
// needs: P7
// status: annotated

struct A {
  virtual void f() requires true;       // error: virtual function cannot be constrained ([temp.constr.decl]) // semantic
};
