// [expr.pre]/4, [expr.const]/5.7: a floating-point operation whose result is
// beyond the type's range gives the infinity, which clang folds without a
// diagnostic; a NaN and a division by zero are not constant.
constexpr float f1 = 1e38f * 3.4028f;
constexpr float f2 = 1e38f * 3.4029f;
constexpr float f3 = 2e38f + 2e38f;
constexpr float f4 = -2e38f - 2e38f;
constexpr float f5 = 1e308 / 1e-308;
static_assert(f2 == f3, "");
static_assert(f4 < 0, "");
static_assert(f5 == f2, "");
constexpr float f6 = f2 - f2; // expected-error {{constexpr variable 'f6' must be initialized by a constant expression}} expected-note {{floating point arithmetic produces a NaN}}
constexpr float f7 = 1.f / 0.f; // expected-error {{constexpr variable 'f7' must be initialized by a constant expression}} expected-note {{division by zero}}
