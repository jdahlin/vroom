// [class.temporary] Example 3 (paragraph 6), docs/cxx-spec/basic/class.temporary.md
// needs: P5
// status: well-formed (as far as the example says)

const int& x = (const int&)1;   // temporary for value 1 has same lifetime as x
