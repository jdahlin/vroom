// [class.mem]/6: a complete-class context of a nested class is also one of the enclosing class: a default argument of the nested class's member function names a member the enclosing class declares later
struct Y {
  struct Nested {
    int mem5(int i = b, int j = c);
    static int c;
  };
  static const int b = 3;
  struct Deeper {
    struct Inner {
      int f(int i = after);
    };
  };
  static const int after = 4;
};
