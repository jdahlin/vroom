// [class.nest] Example 3 (paragraph 3), docs/cxx-spec/class/class.nest.md
// needs: P5
// status: well-formed (as far as the example says)

class E {
  class I1;                     // forward declaration of nested class
  class I2;
  class I1 { };                 // definition of nested class
};
class E::I2 { };                // definition of nested class
