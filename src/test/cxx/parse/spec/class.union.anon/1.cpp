// [class.union.anon] Example 1 (paragraph 1), docs/cxx-spec/class/class.union.anon.md
// needs: P5
// status: well-formed (as far as the example says)

void f() {
  union { int a; const char* p; };
  a = 1;
  p = "Jennifer";
}
