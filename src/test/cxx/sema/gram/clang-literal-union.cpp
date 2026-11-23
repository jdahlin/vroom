// __is_literal of a union ([basic.types]/10, [ext:clang_type_traits]): a union is literal when one member is, but before C++20 not when
// a variant member's class has a non-trivial destructor (the union's is deleted); every value is clang++'s.
struct NonConstexprDtor { constexpr NonConstexprDtor() = default; ~NonConstexprDtor() {} };
union U1 { int a; };
union U2 {};
union U3 { U3() {} ~U3() {} int a; };
union U4 { volatile int a; };
union U5 { U5(int); int a; };
union U6 { NonConstexprDtor x; int y; constexpr U6() : x{} {} };
static_assert(__is_literal(U1));
static_assert(__is_literal(U2));
static_assert(!__is_literal(U3));
static_assert(!__is_literal(U4));
static_assert(!__is_literal(U5));
#if __cplusplus >= 202002L
static_assert(__is_literal(U6));
#else
static_assert(!__is_literal(U6));
#endif
