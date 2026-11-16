// [expr.typeid] Example 1 (paragraph 5), docs/cxx-spec/expr/expr.typeid.md
// needs: P7
// status: well-formed (as far as the example says)

class D { /* ... */ };
D d1;
const D d2;

typeid(d1) == typeid(d2);       // yields true
typeid(D)  == typeid(const D);  // yields true
typeid(D)  == typeid(d2);       // yields true
typeid(D)  == typeid(const D&); // yields true
