// [class.virtual] Example 9 (paragraph 15), docs/cxx-spec/class/class.virtual.md
// needs: P5
// status: well-formed (as far as the example says)

struct VB1a : virtual A {       // does not declare f
};

struct Da : VB1a, VB2 {
};

void foe() {
  VB1a*  vb1ap = new Da;
  vb1ap->f();                   // calls VB2::f
}
