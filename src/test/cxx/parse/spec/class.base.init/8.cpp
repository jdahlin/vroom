// [class.base.init] Example 8 (paragraph 11), docs/cxx-spec/class/class.base.init.md
// needs: P5
// status: annotated

struct A {
  A() = default;        // OK
  A(int v) : v(v) { }   // OK
  const int& v = 42;    // OK
};
A a1;                   // error: ill-formed binding of temporary to reference // semantic
A a2(1);                // OK, unfortunately
