// [expr.const]/5.9, [class.union]/2: a static_assert that reads a union's
// inactive member is not a constant expression
union U {
    int a;
    int b;
};
constexpr U u = {1};
static_assert(u.a == 1, "");
static_assert(u.b == 1, "");
