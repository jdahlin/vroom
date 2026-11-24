// RUN: %clang_cc1 -std=c++11 -fsyntax-only -verify %s
// [impl.clang] __builtin_align_down/_up/is_aligned of an integer: an
// alignment that is not a positive power of two, or more than the operand's
// type can hold, makes the call not a constant expression; the others fold.
template <typename T>
constexpr T align_down(T value, long alignment)
{
    return __builtin_align_down(value, alignment); // expected-note 3 {{requested alignment}}
}
constexpr long value(long l) { return l; }
static_assert(align_down(33, value(16)) == 32, "");
static_assert(align_down(1, value(-1)), ""); // expected-error {{not an integral constant expression}} expected-note {{in call to}}
static_assert(align_down(1, value(17)), ""); // expected-error {{not an integral constant expression}} expected-note {{in call to}}
static_assert(align_down(static_cast<short>(1), value(1 << 20)), ""); // expected-error {{not an integral constant expression}} expected-note {{in call to}}
