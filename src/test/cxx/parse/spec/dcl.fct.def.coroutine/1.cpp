// [dcl.fct.def.coroutine] Example 1 (paragraph 2), docs/cxx-spec/dcl/dcl.fct.def.coroutine.md
// needs: P7
// status: annotated

task<int> f();

task<void> g1() {
  int i = co_await f();
  std::cout << "f() => " << i << std::endl;
}

template <typename... Args>
task<void> g2(Args&&...) {      // OK, ellipsis is a pack expansion
  int i = co_await f();
  std::cout << "f() => " << i << std::endl;
}

task<void> g3(int a, ...) {     // error: variable parameter list not allowed // semantic
  int i = co_await f();
  std::cout << "f() => " << i << std::endl;
}
