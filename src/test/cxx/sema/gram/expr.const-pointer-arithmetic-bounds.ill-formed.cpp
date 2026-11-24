// [expr.add]/4.2, [expr.const]/5.7: a static_assert whose pointer arithmetic
// leaves the array is not a constant expression
constexpr int a[2] = {1, 2};
static_assert(*(a + 1) == 2, "");
static_assert(*(a + 3) == 2, "");
