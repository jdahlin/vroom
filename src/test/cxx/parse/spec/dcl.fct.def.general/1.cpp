// [dcl.fct.def.general] Example 1 (paragraph 3), docs/cxx-spec/dcl/dcl.fct.def.general.md
// needs: P4
// status: well-formed (as far as the example says)

int max(int a, int b, int c) {
  int m = (a > b) ? a : b;
  return (m > c) ? m : c;
}
