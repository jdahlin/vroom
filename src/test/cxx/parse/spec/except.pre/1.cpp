// [except.pre] Example 1 (paragraph 3), docs/cxx-spec/except/except.pre.md
// needs: P6
// status: annotated

void f() {
  goto l1;          // error // semantic
  goto l2;          // error // semantic
  try {
    goto l1;        // OK
    goto l2;        // error // semantic
    l1: ;
  } catch (...) {
    l2: ;
    goto l1;        // error // semantic
    goto l2;        // OK
  }
}
