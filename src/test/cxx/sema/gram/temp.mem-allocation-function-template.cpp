/* [temp.mem], [basic.stc.dynamic.allocation]/1, [class.free]: member function
   templates of operator new and operator delete. */
typedef decltype(sizeof(0)) size_t;

struct A {
  template <typename T> static void *operator new(size_t, T);
  template <typename T> void *operator new[](size_t, T, int);
  template <typename T> static void operator delete(void *, T);
  template <typename T> void operator delete[](void *, T, int);
};

struct B {
  void *operator new(size_t);
  template <typename T> void *operator new(size_t, T);
};

/* [expr.new]/21: the class's allocation function templates are candidates,
   their arguments deduced from the placement arguments. */
struct C {
  template <typename T> static void *operator new(size_t, T *p);
  template <typename T> static void *operator new(size_t, T, int);
};
int use_c(int *ip) {
  C *a = new (ip) C;
  C *b = new (1.5, 2) C;
  return a != b;
}
struct D {
  template <typename T> static void *operator new(size_t, T *);
};
void use_d(int i) { new (i) D; } // expected-error {{no matching function}}
