// [dcl.fct.default] Example 3 (paragraph 5), docs/cxx-spec/dcl/dcl.fct.default.md
// needs: P4
// status: well-formed (as far as the example says)

int a = 1;
int f(int);
int g(int x = f(a));            // default argument: f(::a)

void h() {
  a = 2;
  {
  int a = 3;
  g();                          // g(f(::a))
  }
}
