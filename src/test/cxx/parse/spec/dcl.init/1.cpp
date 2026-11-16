// [dcl.init] Example 1 (paragraph 2), docs/cxx-spec/dcl/dcl.init.md
// needs: P3
// status: well-formed (as far as the example says)

int f(int);
int a = 2;
int b = f(a);
int c(b);
