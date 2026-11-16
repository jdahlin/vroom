// [class.friend] Example 2 (paragraph 2), docs/cxx-spec/class/class.friend.md
// needs: P5
// status: well-formed (as far as the example says)

class A {
  class B { };
  friend class X;
};

struct X : A::B {               // OK: A::B accessible to friend
  A::B mx;                      // OK: A::B accessible to member of friend
  class Y {
    A::B my;                    // OK: A::B accessible to nested member of friend
  };
};
