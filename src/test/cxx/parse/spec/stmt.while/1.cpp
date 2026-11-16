// [stmt.while] Example 1 (paragraph 2), docs/cxx-spec/stmt/stmt.while.md
// needs: P5
// status: well-formed (as far as the example says)

struct A {
  int val;
  A(int i) : val(i) { }
  ~A() { }
  operator bool() { return val != 0; }
};
void example() {
int i = 1;
while (A a = i) {
  // ...
  i = 0;
}
}
