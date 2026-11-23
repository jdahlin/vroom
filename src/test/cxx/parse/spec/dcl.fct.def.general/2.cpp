// [dcl.fct.def.general] Example 2 (paragraph 8), docs/cxx-spec/dcl/dcl.fct.def.general.md
// needs: P5 (__func__ in a ctor-initializer: S is not declared here)
// status: annotated

struct S {
  S() : s(__func__) { }             // OK
  const char* s;
};
void f(const char* s = __func__);   // error: __func__ is undeclared // semantic
