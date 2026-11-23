// clang's type traits on arrays: T[0] ([ext:gnu_zero_length_array]) is no
// array for __is_array and __is_bounded_array; an array's cv-qualifiers are
// its elements' ([basic.type.qualifier]/3) for __is_const and __is_volatile.
// bench/cxx-sema.py compares each verdict with clang's.
typedef int Z[0];
static_assert(!__is_array(Z), "");
static_assert(!__is_bounded_array(Z), "");
static_assert(__is_array(int[1]), "");
static_assert(__is_bounded_array(int[1]), "");
static_assert(__is_unbounded_array(int[]), "");
typedef const void cvoid;
static_assert(__is_const(cvoid), "");
static_assert(__is_const(const int[2]), "");
static_assert(__is_volatile(volatile int[2][3]), "");
static_assert(!__is_const(int[2]), "");
static_assert(!__is_const(const int *), "");
