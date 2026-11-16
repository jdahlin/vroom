// [dcl.stc] Example 2 (paragraph 7), docs/cxx-spec/dcl/dcl.stc.md
// needs: P5
// status: annotated

struct S;
extern S a;
extern S f();
extern void g(S);

void h() {
  g(a);                         // error: S is incomplete // semantic
  f();                          // error: S is incomplete // semantic
}
