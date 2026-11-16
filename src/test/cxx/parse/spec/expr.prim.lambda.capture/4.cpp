// [expr.prim.lambda.capture] Example 4 (paragraph 7), docs/cxx-spec/expr/expr.prim.lambda.capture.md
// needs: P6
// status: well-formed (as far as the example says)

void f(int, const int (&)[2] = {});         // #1
void f(const int&, const int (&)[1]);       // #2
void test() {
  const int x = 17;
  auto g = [](auto a) {
    f(x);                       // OK: calls #1, does not capture x
  };

  auto g1 = [=](auto a) {
    f(x);                       // OK: calls #1, captures x
  };

  auto g2 = [=](auto a) {
    int selector[sizeof(a) == 1 ? 1 : 2]{};
    f(x, selector);             // OK: captures x, might call #1 or #2
  };

  auto g3 = [=](auto a) {
    typeid(a + x);              // captures x regardless of whether a + x is an unevaluated operand
  };
}
