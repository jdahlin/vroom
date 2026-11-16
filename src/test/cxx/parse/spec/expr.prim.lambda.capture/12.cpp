// [expr.prim.lambda.capture] Example 12 (paragraph 17), docs/cxx-spec/expr/expr.prim.lambda.capture.md
// needs: P7
// status: well-formed (as far as the example says)

template<class... Args>
void f(Args... args) {
  auto lm = [&, args...] { return g(args...); };
  lm();

  auto lm2 = [...xs=std::move(args)] { return g(xs...); };
  lm2();
}
