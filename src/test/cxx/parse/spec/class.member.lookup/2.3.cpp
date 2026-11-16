// [class.member.lookup] Example 2, part 3 (paragraph 8), docs/cxx-spec/class/class.member.lookup.md
// needs: P5
// status: well-formed (as far as the example says)

struct C : A, B {
  int f() { return A::f() + B::f(); }
};
