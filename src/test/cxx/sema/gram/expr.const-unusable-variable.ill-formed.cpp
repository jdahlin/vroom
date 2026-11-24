// [expr.const]/5.8: an lvalue-to-rvalue conversion of an object that is not
// usable in constant expressions (a non-constexpr, non-const variable, or a
// non-constexpr class object of static storage duration) is not a constant
// expression; the same read of a usable one is.
int plain = 1;
static_assert(plain == 1, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{read of non-const variable 'plain' is not allowed in a constant expression}}
const int usable = 1;
static_assert(usable == 1, "");
constexpr int cx = 2;
static_assert(cx == 2, "");
struct B { int n; };
const B object = {1};
static_assert(object.n == 1, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{read of non-constexpr variable 'object' is not allowed in a constant expression}}
constexpr B constant_object = {1};
static_assert(constant_object.n == 1, "");
