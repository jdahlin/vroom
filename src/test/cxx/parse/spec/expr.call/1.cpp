// [expr.call] Example 1 (paragraph 7), docs/cxx-spec/expr/expr.call.md
// needs: P7
// status: annotated

template<typename ...T> int f(int n = 0, T ...t);
int x = f<int>();               // error: no argument for second function parameter // semantic
