// [expr.prim.lambda.closure] Example 7 (paragraph 11), docs/cxx-spec/expr/expr.prim.lambda.closure.md
// needs: P6
// status: annotated

auto Fwd = [](int (*fp)(int), auto a) { return fp(a); };
auto C = [](auto a) { return a; };

static_assert(Fwd(C,3) == 3);   // OK

// No specialization of the function call operator template can be constexpr (due to the local static).
auto NC = [](auto a) { static int s; return a; };
static_assert(Fwd(NC,3) == 3);  // error // semantic
