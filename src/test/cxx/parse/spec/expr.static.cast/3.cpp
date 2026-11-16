// [expr.static.cast] Example 3 (paragraph 13), docs/cxx-spec/expr/expr.static.cast.md
// needs: P4
// status: well-formed (as far as the example says)

T* p1 = new T;
const T* p2 = static_cast<const T*>(static_cast<void*>(p1));
bool b = p1 == p2;  // b will have the value true.
