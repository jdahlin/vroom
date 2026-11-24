// [dcl.init.aggr]/5.4.2 (C++20 /5.4): an empty initializer list initializes a
// union's variant member that has a default member initializer, not its
// first member; that member is the active one.
union U { char c; double d = 4.0; };
constexpr U u1 = U();
constexpr U u2 {};
constexpr U u3 { 'x' };
static_assert(u1.d == 4.0, "");
static_assert(u2.d == 4.0, "");
static_assert(u3.c == 'x', "");
static_assert(u3.d == 4.0, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{read of member 'd' of union with active member 'c' is not allowed in a constant expression}}
struct A {
  int n = 5;
  int m = n * 3;
  union {
    char c;
    double d = 4.0;
  };
};
constexpr A a1 {};
constexpr A a2 { 8 };
static_assert(a1.d == 4.0, "");
static_assert(a2.m == 24, "");
static_assert(a2.d == 4.0, "");
static_assert(a1.c == 0, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{read of member 'c' of union with active member 'd' is not allowed in a constant expression}}
