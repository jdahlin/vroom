// [class.bit] Example 1 (paragraph 4), docs/cxx-spec/class/class.bit.md
// needs: P5
// status: well-formed (as far as the example says)

enum BOOL { FALSE=0, TRUE=1 };
struct A {
  BOOL b:1;
};
A a;
void f() {
  a.b = TRUE;
  if (a.b == TRUE)              // yields true
    { /* ... */ }
}
