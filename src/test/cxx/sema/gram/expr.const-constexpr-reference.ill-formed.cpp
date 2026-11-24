// [expr.const]/4, [expr.type]/1: a constexpr reference is usable in constant
// expressions and designates the object it is bound to; reading through it
// follows the rules of [expr.const]/5.
struct B { int v; };
struct D : B { int w; };
constexpr D dd = {{1}, 2};
constexpr const B &br = dd;
static_assert(br.v == 1, "");
static_assert(br.v == 2, ""); // expected-error {{static assertion failed}} expected-note {{evaluates to '1 == 2'}}
constexpr int arr[3] = {1, 2, 3};
constexpr const int &r2 = arr[1];
static_assert(r2 == 2, "");
int nc = 5; // expected-note {{declared here}}
constexpr int &rn = nc;
static_assert(rn == 5, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{read of non-const variable 'nc' is not allowed in a constant expression}}
