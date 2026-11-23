// __is_nothrow_convertible of scalars ([meta.rel], [ext:clang_type_traits]); values are clang++'s.
static_assert(__is_nothrow_convertible(int, long));
static_assert(!__is_nothrow_convertible(int, void));
static_assert(__is_nothrow_convertible(void, void));
static_assert(__is_nothrow_convertible(int*, void*));
static_assert(!__is_nothrow_convertible(void*, int*));
static_assert(__is_nothrow_convertible(int[3], int*));
static_assert(!__is_nothrow_convertible(int, int&));
static_assert(__is_nothrow_convertible(int&, int));
static_assert(!__is_nothrow_convertible(int&, int&&));
static_assert(!__is_nothrow_convertible(const int&, int&&));
static_assert(!__is_nothrow_convertible(long, int*));
static_assert(__is_nothrow_convertible(double, int));
static_assert(__is_nothrow_convertible(decltype(nullptr), int*));
static_assert(__is_nothrow_convertible(int(), int(*)()));
static_assert(!__is_nothrow_convertible(int(), int()));
static_assert(__is_nothrow_convertible(int, bool));
static_assert(__is_nothrow_convertible(float, double));
