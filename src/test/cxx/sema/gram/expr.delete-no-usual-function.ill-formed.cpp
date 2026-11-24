// [expr.delete]/9, [basic.stc.dynamic.deallocation]/2: the class's deallocation functions are found and none is usual.
typedef decltype(sizeof(0)) size_t;
struct A {
  static void operator delete(void *, int);
};
struct B {
  static void operator delete(void *);
  static void operator delete(void *, int);
};
void f(A *a) { delete a; }
void g(B *b) { delete b; }
