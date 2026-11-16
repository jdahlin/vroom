// [class.virtual] Example 7 (paragraph 13), docs/cxx-spec/class/class.virtual.md
// needs: P5
// status: annotated

struct A {
  virtual void f();
};

struct B1 : A {                 // note non-virtual derivation
  void f();
};

struct B2 : A {
  void f();
};

struct D : B1, B2 {             // D has two separate A subobjects
};

void foo() {
  D   d;
//A* ap = &d; // would be ill-formed: ambiguous // semantic
  B1*  b1p = &d;
  A*   ap = b1p;
  D*   dp = &d;
  ap->f();                      // calls D::B1::f
  dp->f();                      // error: ambiguous // semantic
}
