// [temp.variadic]/5, [temp.param]/14: a function parameter pack need not be
// the last parameter of a function template, a constructor template or a
// declaration of a template; its template parameter pack is then a
// non-deduced context and given explicitly or empty.
template <typename A, typename... T> void f(T..., A);
template <typename... T> void g(T..., int);
template <typename... T> void g(T..., int) {}
struct S {
  template <typename... T> S(T..., int);
};
