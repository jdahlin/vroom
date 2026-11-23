// [class.access.spec] Example 3 (paragraph 4), docs/cxx-spec/class/class.access.spec.md
// needs: P5
// status: annotated

struct S {
  class A;
  enum E : int;
private:
  class A { };                  // error: cannot change access // semantic
  enum E: int { e0 };           // error: cannot change access // semantic
};
