// [expr.const]/13 (/11 before C++23): the result of a constant expression
// that refers to a temporary of the evaluation (a reference member bound to
// an argument's temporary by a constructor) is not a constant expression; a
// reference bound to a variable of static storage duration is.
struct T {
  constexpr T(const int &r) : r(r) {}
  const int &r;
};
constexpr int n = 0;
constexpr T t1(n);
static_assert(t1.r == 0, "");
constexpr T t2(0); // expected-error {{constexpr variable 't2' must be initialized by a constant expression}} expected-note {{temporary created here}} expected-note {{reference to temporary is not a constant expression}}
