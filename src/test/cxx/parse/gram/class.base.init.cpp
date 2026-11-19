// [class.base.init]/2: a mem-initializer-id names a member, and the members
// of an unnamed struct or union are the enclosing class's
// ([class.union.anon]/1; the struct is clang's extension).
template <class T> struct U {
  struct { T a; int b; };
  U() : a(), b(3) {}
};

template <class T> struct W {
  union { T a; int b; };
  int c;
  W() : c(3), b(1) {}
};
