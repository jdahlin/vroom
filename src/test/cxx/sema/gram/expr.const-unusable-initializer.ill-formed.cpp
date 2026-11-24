// [dcl.constexpr]/10, [expr.const]/5.8: a constexpr variable whose
// initializer is not a constant expression is not usable in constant
// expressions; a read of it is not a constant expression either.
constexpr int one = 1;
static_assert(one == 1, "");
constexpr int bad = 1 / 0; // expected-error {{constexpr variable 'bad' must be initialized by a constant expression}} expected-note {{division by zero}}
static_assert(bad == 0, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{initializer of 'bad' is not a constant expression}}
