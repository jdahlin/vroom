// [class.conv.fct] Example 1 (paragraph 1), docs/cxx-spec/class/class.conv.fct.md
// needs: P5
// status: annotated

struct X {
  operator int();
  operator auto() -> short;     // error: trailing return type // semantic
};

void f(X a) {
  int i = int(a);
  i = (int)a;
  i = a;
}
