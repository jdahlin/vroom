// [expr.add]/4, [expr.const]/5: an array of unknown bound declared and not
// defined (`extern int arr[]`): the address of its first element is a
// constant, an element after it is not known to exist (clang: "indexing of
// array without known bound").
extern int arr[];
constexpr int *a = arr;
constexpr int *b = &arr[0];
static_assert(a == b, "");
constexpr int *c = &arr[1]; // expected-error {{constexpr variable 'c' must be initialized by a constant expression}} expected-note {{indexing of array without known bound is not allowed in a constant expression}}
constexpr int *d = &a[1]; // expected-error {{constexpr variable 'd' must be initialized by a constant expression}} expected-note {{indexing of array without known bound is not allowed in a constant expression}}
constexpr int *e = a + 1; // expected-error {{constexpr variable 'e' must be initialized by a constant expression}} expected-note {{indexing of array without known bound is not allowed in a constant expression}}
struct Y { int a; };
extern Y yarr[];
constexpr Y *p = yarr;
constexpr int *q = &p->a;
extern const int carr[]; // expected-note {{declared here}}
constexpr int n = carr[0]; // expected-error {{constexpr variable 'n' must be initialized by a constant expression}} expected-note {{read of non-constexpr variable 'carr' is not allowed in a constant expression}}
