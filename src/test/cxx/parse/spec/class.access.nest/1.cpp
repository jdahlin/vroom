// [class.access.nest] Example 1 (paragraph 1), docs/cxx-spec/class/class.access.nest.md
// needs: P5
// status: annotated

class E {
  int x;
  class B { };

  class I {
    B b;                        // OK: E::I can access E::B
    int y;
    void f(E* p, int i) {
      p->x = i;                 // OK: E::I can access E::x
    }
  };

  int g(I* p) {
    return p->y;                // error: I::y is private // semantic
  }
};
