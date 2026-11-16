// [dcl.fct.default] Example 7 (paragraph 9), docs/cxx-spec/dcl/dcl.fct.default.md
// needs: P3
// status: annotated

int a;
int f(int a, int b = a);            // error: parameter a used as default argument // semantic
typedef int I;
int g(float I, int b = I(2));       // error: parameter I found // semantic
int h(int a, int b = sizeof(a));    // OK, unevaluated operand
