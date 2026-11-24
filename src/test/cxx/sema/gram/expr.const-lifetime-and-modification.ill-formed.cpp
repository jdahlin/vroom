// [expr.const]/5.8, /5.16: the read of an object whose lifetime ended
// (basic.life) and the modification of an object that began its life outside
// the evaluation are not constant expressions.
constexpr int dangling() { int *p; { int x = 1; p = &x; } return *p; }
static_assert(dangling() == 1, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{read of object outside its lifetime is not allowed in a constant expression}}
int visible = 0;
constexpr int modify() { visible = 1; return 0; } // expected-error {{never produces a constant expression}} expected-note {{assignment to non-constexpr variable 'visible'}}
static_assert(modify() == 0, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{modification of object of non-constexpr type is not allowed}}
constexpr int local() { int x = 0; x = 1; return x; }
static_assert(local() == 1, "");
