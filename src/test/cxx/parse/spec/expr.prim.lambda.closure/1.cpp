// [expr.prim.lambda.closure] Example 1 (paragraph 3), docs/cxx-spec/expr/expr.prim.lambda.closure.md
// needs: P6
// status: well-formed (as far as the example says)

auto glambda = [](auto a, auto&& b) { return a < b; };
bool b = glambda(3, 3.14);                                      // OK

auto vglambda = [](auto printer) {
  return [=](auto&& ... ts) {                                   // OK: ts is a function parameter pack
    printer(std::forward<decltype(ts)>(ts)...);

    return [=]() {
      printer(ts ...);
    };
  };
};
auto p = vglambda( [](auto v1, auto v2, auto v3)
                   { std::cout << v1 << v2 << v3; } );
auto q = p(1, 'a', 3.14);                                       // OK: outputs 1a3.14
q();                                                            // OK: outputs 1a3.14
