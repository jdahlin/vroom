// [expr.rel]/4, [expr.const]/5: a relational comparison of pointers to
// different complete objects, or of a pointer and a null pointer, has an
// unspecified result and is not a constant expression; equality is
// specified, as is a comparison of two members of one object.
int a, b;
constexpr int *p = &a, *q = &b;
constexpr bool u1 = p < q; // expected-error {{constexpr variable 'u1' must be initialized by a constant expression}} expected-note {{comparison between pointers to unrelated objects '&a' and '&b' has unspecified value}}
constexpr bool u2 = p >= (int *)0; // expected-error {{constexpr variable 'u2' must be initialized by a constant expression}} expected-note {{comparison between pointers to unrelated objects '&a' and 'nullptr' has unspecified value}}
constexpr bool e1 = p == q;
constexpr bool e2 = p != (int *)0;
constexpr bool same = p <= p;
