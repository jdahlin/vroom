// [class.base.init] Example 7 (paragraph 10), docs/cxx-spec/class/class.base.init.md
// needs: P5
// status: well-formed (as far as the example says)

struct A {
  int i = /* some integer expression with side effects */ ;
  A(int arg) : i(arg) { }
  // ...
};
