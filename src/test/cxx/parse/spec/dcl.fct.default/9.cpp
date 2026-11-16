// [dcl.fct.default] Example 9 (paragraph 9), docs/cxx-spec/dcl/dcl.fct.default.md
// needs: P4
// status: annotated

int f(int = 0);

void h() {
  int j = f(1);
  int k = f();                      // OK, means f(0)
}

int (*p1)(int) = &f;
int (*p2)() = &f;                   // error: type mismatch // semantic
