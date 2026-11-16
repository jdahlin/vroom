// [class.friend] Example 4 (paragraph 2), docs/cxx-spec/class/class.friend.md
// needs: P5
// status: annotated

class A {
  friend class B { };           // error: cannot define class in friend declaration // syntax-error
};
