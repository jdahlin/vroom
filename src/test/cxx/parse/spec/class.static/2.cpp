// [class.static] Example 2 (paragraph 2), docs/cxx-spec/class/class.static.md
// needs: P5
// status: well-formed (as far as the example says)

int g();
struct X {
  static int g();
};
struct Y : X {
  static int i;
};
int Y::i = g();                 // equivalent to Y::g();
