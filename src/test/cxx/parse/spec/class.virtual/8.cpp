// [class.virtual] Example 8 (paragraph 14), docs/cxx-spec/class/class.virtual.md
// needs: P5
// status: annotated

struct A {
  virtual void f();
};

struct VB1 : virtual A {        // note virtual derivation
  void f();
};

struct VB2 : virtual A {
  void f();
};

struct Error : VB1, VB2 {       // error // semantic
};

struct Okay : VB1, VB2 {
  void f();
};
