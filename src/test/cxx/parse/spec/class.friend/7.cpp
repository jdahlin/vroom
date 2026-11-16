// [class.friend] Example 7 (paragraph 6), docs/cxx-spec/class/class.friend.md
// needs: P5
// status: well-formed (as far as the example says)

class M {
  friend void f() { }           // definition of global f, a friend of M,
                                // not the definition of a member function
};
