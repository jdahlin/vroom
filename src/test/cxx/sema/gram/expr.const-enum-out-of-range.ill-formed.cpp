// [expr.static.cast]/10 and [expr.const]/5: a value outside the range of the
// values of an enumeration without a fixed underlying type ([dcl.enum]/8) is
// undefined behavior, hence no constant expression.
enum E1 { e11 = -4, e12 = 4 };
enum E2 { e21 = 0, e22 = 4 };
enum EEmpty {};
enum EFixed : int { efixed = -4 };
constexpr E1 a = static_cast<E1>(-8);
constexpr E1 b = static_cast<E1>(8); // expected-error {{constexpr variable 'b' must be initialized by a constant expression}} expected-note {{integer value 8 is outside the valid range of values [-8, 7] for the enumeration type 'E1'}}
constexpr E2 c = static_cast<E2>(-8); // expected-error {{constexpr variable 'c' must be initialized by a constant expression}} expected-note {{integer value -8 is outside the valid range of values [0, 7] for the enumeration type 'E2'}}
constexpr E2 d = static_cast<E2>(7);
constexpr EEmpty e = static_cast<EEmpty>(2); // expected-error {{constexpr variable 'e' must be initialized by a constant expression}} expected-note {{integer value 2 is outside the valid range of values [0, 1] for the enumeration type 'EEmpty'}}
constexpr EFixed f = static_cast<EFixed>(100);
E1 g = static_cast<E1>(8);
