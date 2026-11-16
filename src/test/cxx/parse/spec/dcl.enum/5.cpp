// [dcl.enum] Example 5 (paragraph 11), docs/cxx-spec/dcl/dcl.enum.md
// needs: P5
// status: annotated

struct X {
  enum direction { left='l', right='r' };
  int f(int i) { return i==left ? 0 : i==right ? 1 : 2; }
};

void g(X* p) {
  direction d;                  // error: direction not in scope // semantic
  int i;
  i = p->f(left);               // error: left not in scope // semantic
  i = p->f(X::right);           // OK
  i = p->f(p->left);            // OK
  // ...
}
