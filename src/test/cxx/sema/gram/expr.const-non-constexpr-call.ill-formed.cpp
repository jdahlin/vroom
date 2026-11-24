// [expr.const]/5.2: a static_assert that calls a function that is not
// constexpr is not a constant expression
constexpr int f(int x) { return x; }
int g(int x) { return x; }
static_assert(f(1) == 1, "");
static_assert(g(1) == 1, "");
