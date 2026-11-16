// [expr.prim.lambda.capture] Example 11 (paragraph 14), docs/cxx-spec/expr/expr.prim.lambda.capture.md
// needs: P6 (std::cout is not declared: those statements stay unparsed)
// status: well-formed (as far as the example says)
void example() {

int a = 1, b = 1, c = 1;
auto m1 = [a, &b, &c]() mutable {
  auto m2 = [a, b, &c]() mutable {
    std::cout << a << b << c;
    a = 4; b = 4; c = 4;
  };
  a = 3; b = 3; c = 3;
  m2();
};
a = 2; b = 2; c = 2;
m1();
std::cout << a << b << c;
}
