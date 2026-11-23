// [class.this] Example 1 (paragraph 1), docs/cxx-spec/class/class.this.md
// needs: P5
// status: annotated

struct s {
  int a;
  int f() const;
  int g() { return a++; }
  int h() const { return a++; } // error // semantic
};

int s::f() const { return a; }
