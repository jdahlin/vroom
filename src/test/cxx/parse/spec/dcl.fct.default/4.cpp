// [dcl.fct.default] Example 4 (paragraph 6), docs/cxx-spec/dcl/dcl.fct.default.md
// needs: P5
// status: annotated

class C {
  void f(int i = 3);
  void g(int i, int j = 99);
};

void C::f(int i = 3) {}         // error: default argument already specified in class scope // semantic
void C::g(int i = 88, int j) {} // in this translation unit, C::g can be called with no argument
