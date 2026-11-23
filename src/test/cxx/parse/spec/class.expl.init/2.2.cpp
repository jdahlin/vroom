// [class.expl.init] Example 2, part 2 (paragraph 2), docs/cxx-spec/class/class.expl.init.md
// needs: P5 (names declared in another part or outside the example stay unparsed)
// status: well-formed (as far as the example says)

struct X {
  int i;
  float f;
  complex c;
} x = { 99, 88.8, 77.7 };
