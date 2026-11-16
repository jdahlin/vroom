// [stmt.iter] Example 2 (paragraph 3), docs/cxx-spec/stmt/stmt.iter.md
// needs: P4
// status: annotated

void f() {
  for (int i = 0; i < 10; ++i)
    int i = 0;          // error: redeclaration // semantic
  for (int i : { 1, 2, 3 })
    int i = 1;          // error: redeclaration // semantic
}
