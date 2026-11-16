// [class.base.init] Example 10 (paragraph 15), docs/cxx-spec/class/class.base.init.md
// needs: P5
// status: well-formed (as far as the example says)

class X {
  int a;
  int b;
  int i;
  int j;
public:
  const int& r;
  X(int i): r(a), b(i), i(i), j(this->i) { }
};
