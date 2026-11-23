// [expr.call] Example 4 (paragraph 8), docs/cxx-spec/expr/expr.call.md
// needs: P5
// status: well-formed (as far as the example says)

struct S {
  S(int);
};
int operator<<(S, int);
int i, j;
int x = S(i=1) << (i=2);
int y = operator<<(S(j=1), j=2);
