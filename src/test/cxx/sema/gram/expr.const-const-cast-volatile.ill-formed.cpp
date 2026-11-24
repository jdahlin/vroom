// [expr.const]/5.8, [expr.const.cast]/4: a const_cast of a glvalue to a
// reference does not make the volatile object it designates non-volatile; its
// read is not a constant expression.
volatile const int vi = 1;
struct S {
  constexpr S() : i(1), v(const_cast<volatile int &>(vi)) {}
  int i;
  volatile int &v;
};
constexpr volatile S vs; // expected-note {{here}}
static_assert(const_cast<int &>(vs.i), ""); // expected-error {{constant expression}} expected-note {{read of volatile object 'vs'}}
