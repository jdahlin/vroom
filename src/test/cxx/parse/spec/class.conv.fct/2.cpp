// [class.conv.fct] Example 2 (paragraph 2), docs/cxx-spec/class/class.conv.fct.md
// needs: P5 (names declared in another part or outside the example stay unparsed)
// status: annotated

class Y { };
struct Z {
  explicit operator Y() const;
};

void h(Z z) {
  Y y1(z);          // OK: direct-initialization
  Y y2 = z;         // error: no conversion function candidate for copy-initialization // semantic
  Y y3 = (Y)z;      // OK: cast notation
}

void g(X a, X b) {
  int i = (a) ? 1+a : 0;
  int j = (a&&b) ? a+b : i;
  if (a) {
  }
}
