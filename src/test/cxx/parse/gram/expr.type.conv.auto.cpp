// RUN: %clang_cc1 -std=c++23 %s
// P0849R8 (C++23), [expr.type.conv]: auto ( expression-list_opt ) and
// auto braced-init-list are simple-type-specifier forms of an explicit type
// conversion (the decay-copy).  The parse keeps `auto` as the specifier.
struct A {
  A(const A &);
};
void take(A);
void use(A a, int i) {
  take(auto(a));
  take(auto{a});
  int j = auto(i) + 1;
  (void)auto{i};
  (void)j;
}
template <class T> void generic(T t) {
  T u = auto(t);
  T v = auto{t};
  (void)u;
  (void)v;
}
