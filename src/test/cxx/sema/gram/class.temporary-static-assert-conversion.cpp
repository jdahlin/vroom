// [class.temporary]/2, [expr.const]: a conversion function called on an
// aggregate prvalue in a static_assert, at namespace scope: the temporary
// lives only in the constant evaluation.
struct T { constexpr operator bool() const { return true; } };
struct U { constexpr operator int() const { return 1; } };
static_assert(T(), "");
static_assert(U() == 1, "");
