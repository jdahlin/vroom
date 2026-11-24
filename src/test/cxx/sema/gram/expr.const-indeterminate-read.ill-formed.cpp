// [expr.const]/5.8, [basic.indet]/2: a static_assert that reads an
// indeterminate value is not a constant expression
constexpr int f(bool b)
{
    int x;
    if (b)
        x = 1;
    return x;
}
static_assert(f(true) == 1, "");
static_assert(f(false) == 1, "");
