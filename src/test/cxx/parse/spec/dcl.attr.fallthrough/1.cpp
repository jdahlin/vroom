// [dcl.attr.fallthrough] Example 1 (paragraph 3), docs/cxx-spec/dcl/dcl.attr.fallthrough.md
// needs: P4
// status: annotated

void f(int n) {
  void g(), h(), i();
  switch (n) {
  case 1:
  case 2:
    g();
    [[fallthrough]];
  case 3:                       // warning on fallthrough discouraged
    do {
      [[fallthrough]];          // error: next statement is not part of the same substatement execution // semantic
    } while (false);
  case 6:
    do {
      [[fallthrough]];          // error: next statement is not part of the same substatement execution // semantic
    } while (n--);
  case 7:
    while (false) {
      [[fallthrough]];          // error: next statement is not part of the same substatement execution // semantic
    }
  case 5:
    h();
  case 4:                       // implementation may warn on fallthrough
    i();
    [[fallthrough]];            // error // semantic
  }
}
