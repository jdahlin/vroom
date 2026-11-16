// [expr.prim.id] Example 2 (paragraph 4), docs/cxx-spec/expr/expr.prim.id.md
// needs: P7
// status: annotated

template<typename T> struct A {
  static void f(int) requires false;
}

void g() {
  A<int>::f(0);                         // error: cannot call f // semantic
  void (*p1)(int) = A<int>::f;          // error: cannot take the address of f // semantic
  decltype(A<int>::f)* p2 = nullptr;    // error: the type decltype(A<int>::f) is invalid // semantic
}
