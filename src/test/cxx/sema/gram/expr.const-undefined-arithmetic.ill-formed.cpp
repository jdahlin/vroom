// [expr.const]/5.7, [expr.mul]/4, [conv.fpint]/1: an operation whose result
// is undefined (division by zero, a floating value that cannot be
// represented in the integer type) is not a constant expression; the same
// operations with defined results are.
static_assert(7 / 2 == 3, "");
static_assert(7 / 0 == 0, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{division by zero}}
static_assert(7 % 0 == 0, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{division by zero}}
static_assert((int)3.5 == 3, "");
static_assert((int)1e30 == 0, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{value 1.0E+30 is outside the range of representable values of type 'int'}}
static_assert((unsigned)-2.0 == 0, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{value -2 is outside the range of representable values of type 'unsigned int'}}
