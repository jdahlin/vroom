// [expr.prim.this] Example 1 (paragraph 2), docs/cxx-spec/expr/expr.prim.this.md
// needs: P7
// status: well-formed (as far as the example says)

struct A {
  char g();
  template<class T> auto f(T t) -> decltype(t + g())
    { return t + g(); }
};
template auto A::f(int t) -> decltype(t + g());
