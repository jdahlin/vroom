// [expr.const]/5.8, [expr.add]/4: reading the element one past the end of
// an array designates no object, so it is not a constant expression; the
// address of that element is.
constexpr int arr[3] = {1, 2, 3};
static_assert(arr[2] == 3, "");
static_assert(arr[3] == 0, ""); // expected-error {{static assertion expression is not an integral constant expression}} expected-note {{read of dereferenced one-past-the-end pointer is not allowed in a constant expression}}
constexpr const int *past = &arr[3];
constexpr const int *end = arr + 3;
static_assert(past == end, "");
