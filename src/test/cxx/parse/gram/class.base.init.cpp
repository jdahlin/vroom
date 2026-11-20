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

// ... also those declared after the constructor: its mem-initializers are a
// complete-class context ([class.mem]/6).
template <class T> struct Late {
  Late() : a(), b(3) {}
  T get() { return a; }
  struct { T a; int b; };
};

// [class.derived]/2: a base-specifier's class-or-decltype after a
// nested-name-specifier names a member of the class, which may come from a
// base the parser does not know (a dependent base, libc++'s common_type).
template <class A> struct Unknown : A {};
template <class A> struct FromUnknown : Unknown<decltype(A())>::type {};
template <class A> struct FromUnknown2 : Unknown<A>::type {};
