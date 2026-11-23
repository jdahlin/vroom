// [dcl.constexpr]/10, [dcl.init]/7: a constexpr object of a class with a
// default constructor that does something is default-initialized by it,
// which must be a constant evaluation; a constructor whose parameters all
// have default arguments is a default constructor, so the class is
// const-default-constructible ([dcl.init]/7.3).
struct Q {
  int a, b;
  constexpr Q() : a(1), b(2) {}
};
constexpr Q q;
struct D {
  int i;
  D(int = 0) : i(1) {}
};
const D d;
