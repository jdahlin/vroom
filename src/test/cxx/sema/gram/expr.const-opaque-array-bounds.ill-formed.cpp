// [expr.add]/4, [expr.const]/5.7: an array object the evaluation does not
// hold the elements of (not constexpr, or declared and not defined) still
// has its bound: an element past it is not a constant expression.
int arr[3];
extern int ext[3];
constexpr int *a = arr + 5; // expected-error {{constexpr variable 'a' must be initialized by a constant expression}} expected-note {{cannot refer to element 5 of array of 3 elements in a constant expression}}
constexpr int *b = &arr[4]; // expected-error {{constexpr variable 'b' must be initialized by a constant expression}} expected-note {{cannot refer to element 4 of array of 3 elements in a constant expression}}
constexpr int *c = &arr[3];
constexpr int *d = ext + 5; // expected-error {{constexpr variable 'd' must be initialized by a constant expression}} expected-note {{cannot refer to element 5 of array of 3 elements in a constant expression}}
constexpr int *e = ext + 3;
constexpr int *g = &ext[-1]; // expected-error {{constexpr variable 'g' must be initialized by a constant expression}} expected-note {{cannot refer to element -1 of array of 3 elements in a constant expression}}
