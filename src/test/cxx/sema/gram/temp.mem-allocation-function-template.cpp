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
