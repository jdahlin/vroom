// [class.name] Example 5 (paragraph 3), docs/cxx-spec/class/class.name.md
// needs: P5
// status: well-formed (as far as the example says)

struct s { int a; };

void g(int s) {
  struct s* p = new struct s;   // global s
  p->a = s;                     // parameter s
}
