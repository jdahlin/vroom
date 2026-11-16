// [class.copy.ctor] Example 1 (paragraph 1), docs/cxx-spec/class/class.copy.ctor.md
// needs: P5
// status: well-formed (as far as the example says)

struct X {
  X(int);
  X(const X&, int = 1);
};
X a(1);             // calls X(int);
X b(a, 0);          // calls X(const X&, int);
X c = b;            // calls X(const X&, int);
