// __is_complete_type ([ext:clang_type_traits]): void, incomplete classes and arrays of unknown bound are not complete; every value is clang++'s.
struct I; struct C{}; 
static_assert(!__is_complete_type(void)); static_assert(!__is_complete_type(const void));static_assert(__is_complete_type(float));
static_assert(!__is_complete_type(I));static_assert(__is_complete_type(C));static_assert(!__is_complete_type(int[]));static_assert(__is_complete_type(int[3]));
static_assert(__is_complete_type(int()));static_assert(__is_complete_type(int&));static_assert(!__is_complete_type(I[3]));static_assert(__is_complete_type(I*));
