// [dcl.fct] Example 9 (paragraph 21), docs/cxx-spec/dcl/dcl.fct.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

template<typename... T> void f(T (* ...t)(int, int));

int add(int, int);
float subtract(int, int);

void g() {
  f(add, subtract);
}
