// [expr.const]/5.7: a static_assert that dereferences a null pointer is not a constant expression
constexpr int a[2] = {1, 2};
constexpr const int *p = a;
static_assert(*p == 1, "");
constexpr const int *n = nullptr;
static_assert(*n == 1, "");
