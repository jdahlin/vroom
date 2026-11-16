// [stmt.dcl] Example 1 (paragraph 3), docs/cxx-spec/stmt/stmt.dcl.md
// needs: P4 (X is not declared: its declaration stays unparsed)
// status: annotated

void f() {
  // ...
  goto lx;          // error: jump into scope of a // semantic
  // ...
ly:
  X a = 1;
  // ...
lx:
  goto ly;          // OK, jump implies destructor call for a followed by
                    // construction again immediately following label ly
}
