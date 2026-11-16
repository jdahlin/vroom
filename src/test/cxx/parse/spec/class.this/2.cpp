// [class.this] Example 2 (paragraph 3), docs/cxx-spec/class/class.this.md
// needs: P5 (names declared in another part or outside the example stay unparsed)
// status: annotated

void k(s& x, const s& y) {
  x.f();
  x.g();
  y.f();
  y.g();                        // error // semantic
}
