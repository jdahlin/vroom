// [dcl.stc] Example 2 (paragraph 7), docs/cxx-spec/dcl/dcl.stc.md
// needs: (the step whose grammar parses it)
// status: unannotated (mark each error line // syntax-error or // semantic)

struct S;
extern S a;
extern S f();
extern void g(S);

void h() {
  g(a);                         // error: S is incomplete
  f();                          // error: S is incomplete
}
