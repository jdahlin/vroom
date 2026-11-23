// [dcl.fct.default] Example 5 (paragraph 7), docs/cxx-spec/dcl/dcl.fct.default.md
// needs: P4
// status: annotated

void f() {
  int i;
  extern void g(int x = i);         // error // semantic
  extern void h(int x = sizeof(i)); // OK
  // ...
}
