// [class.nest] Example 2 (paragraph 2), docs/cxx-spec/class/class.nest.md
// needs: P5
// status: well-formed (as far as the example says)

struct enclose {
  struct inner {
    static int x;
    void f(int i);
  };
};

int enclose::inner::x = 1;

void enclose::inner::f(int i) { /* ... */ }
