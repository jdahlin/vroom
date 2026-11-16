// [dcl.init] Example 2 (paragraph 13), docs/cxx-spec/dcl/dcl.init.md
// needs: P5
// status: well-formed (as far as the example says)

int a;

struct X {
  static int a;
  static int b;
};

int X::a = 1;
int X::b = a;                   // X::b = X::a
