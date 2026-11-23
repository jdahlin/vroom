// [class.mfct] Example 1 (paragraph 3), docs/cxx-spec/class/class.mfct.md
// needs: P5
// status: well-formed (as far as the example says)

struct X {
  typedef int T;
  static T count;
  void f(T);
};
void X::f(T t = count) { }
