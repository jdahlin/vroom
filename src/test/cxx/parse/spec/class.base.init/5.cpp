// [class.base.init] Example 5 (paragraph 8), docs/cxx-spec/class/class.base.init.md
// needs: P5
// status: annotated

struct A {
  A() : v(42) { }   // error // semantic
  const int& v;
};
