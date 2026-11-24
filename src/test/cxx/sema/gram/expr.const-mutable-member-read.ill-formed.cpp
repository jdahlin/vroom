// [expr.const]/5.9: the read of a mutable member of an object that began its
// life outside the evaluation is not a constant expression; the read of a
// non-mutable member is.
struct MM {
  mutable int n;
  int m;
} constexpr mm = {4, 5};
constexpr int mmn = mm.n; // expected-error {{constexpr variable 'mmn' must be initialized by a constant expression}} expected-note {{read of mutable member 'n' is not allowed in a constant expression}}
static_assert(mm.n == 4, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{read of mutable member 'n' is not allowed in a constant expression}}
constexpr int mmm = mm.m;
