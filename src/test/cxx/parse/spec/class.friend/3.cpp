// [class.friend] Example 3 (paragraph 2), docs/cxx-spec/class/class.friend.md
// needs: P5
// status: annotated

class X {
  enum { a=100 };
  friend class Y;
};

class Y {
  int v[X::a];                  // OK, Y is a friend of X
};

class Z {
  int v[X::a];                  // error: X::a is private // semantic
};
