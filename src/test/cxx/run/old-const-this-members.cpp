// a data member named alone in a const member function is const (not a mutable one): t.f() calls the
// const overload (map::begin() const: __tree_.begin() const); decltype(m) stays as declared
struct T { int f() { return 1; } int f() const { return 2; } };
template <class X> struct M {
  X t;
  int g() const { return t.f(); }
  int h() { return t.f(); }
};
struct N { T t; int g() const { return t.f(); } };
template <class A, class B> struct same { static constexpr bool v = false; };
template <class A> struct same<A, A> { static constexpr bool v = true; };
struct S { int m; mutable int k; int &r; int g() const { static_assert(same<decltype(m), int>::v); static_assert(same<decltype((m)), const int&>::v); static_assert(same<decltype((k)), int&>::v); return m; } };
int main() { M<T> m; N n; int z = 0; S s{1, 2, z}; return m.g() == 2 && m.h() == 1 && n.g() == 2 && s.g() == 1 ? 0 : 1; }
