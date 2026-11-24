// [expr.const]/13.1 (/11 before C++23): a constexpr reference to an object
// of automatic storage duration is not a constant expression (its address
// differs per invocation); one to an object of static storage duration is.
int g;
struct S { int m; };
S gs;
void f(int param) { // expected-note {{declared here}}
  static int s;
  constexpr const int &a = s;
  constexpr const int &b = g;
  constexpr int &c = gs.m;
  int local; // expected-note {{declared here}}
  constexpr const int &d = local; // expected-error {{constexpr variable 'd' must be initialized by a constant expression}} expected-note {{reference to 'local' is not a constant expression}}
  constexpr const int &e = param; // expected-error {{constexpr variable 'e' must be initialized by a constant expression}} expected-note {{reference to 'param' is not a constant expression}}
  S ls = {1}; // expected-note {{declared here}}
  constexpr const int &h = ls.m; // expected-error {{constexpr variable 'h' must be initialized by a constant expression}} expected-note {{reference to subobject of 'ls' is not a constant expression}}
}
