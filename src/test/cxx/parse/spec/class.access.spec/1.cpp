// [class.access.spec] Example 1 (paragraph 1), docs/cxx-spec/class/class.access.spec.md
// needs: P5
// status: well-formed (as far as the example says)

class X {
  int a;            // X::a is private by default: class used
public:
  int b;            // X::b is public
  int c;            // X::c is public
};
