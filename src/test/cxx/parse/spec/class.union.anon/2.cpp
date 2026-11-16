// [class.union.anon] Example 2 (paragraph 3), docs/cxx-spec/class/class.union.anon.md
// needs: P5
// status: annotated

void f() {
  union { int aa; char* p; } obj, *ptr = &obj;
  aa = 1;           // error // semantic
  ptr->aa = 1;      // OK
}
