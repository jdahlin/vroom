// [expr.const]/5.15 (before C++26): a conversion from cv void* to a
// pointer-to-object type is not a constant expression.
constexpr void *p = nullptr;
constexpr int *q = static_cast<int *>(p); // expected-error {{constexpr variable 'q' must be initialized by a constant expression}} expected-note {{cast from 'void *' is not allowed in a constant expression}}
