// [class.copy.assign] Example 1 (paragraph 1), docs/cxx-spec/class/class.copy.assign.md
// needs: P5
// status: annotated

struct X {
  X();
  X& operator=(X&);
};
const X cx;
X x;
void f() {
  x = cx;           // error: X::operator=(X&) cannot assign cx into x // semantic
}
