// [class.friend] Example 8 (paragraph 10), docs/cxx-spec/class/class.friend.md
// needs: P5
// status: annotated

class A {
  friend class B;
  int a;
};

class B {
  friend class C;
};

class C  {
  void f(A* p) {
    p->a++;         // error: C is not a friend of A despite being a friend of a friend // semantic
  }
};

class D : public B  {
  void f(A* p) {
    p->a++;         // error: D is not a friend of A despite being derived from a friend // semantic
  }
};
