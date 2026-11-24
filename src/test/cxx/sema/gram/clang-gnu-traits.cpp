// the deprecated GNU traits __has_trivial_move_* and __has_nothrow_* of scalars and trivial classes (clang's values; classes with their own copy or move members stay unsupported)
struct A{};
struct G{ A b; int x; };
struct M{ M(); M(int); };
struct N{ N() noexcept; N(int); };
struct F{ A a; int arr[4]; };
#define Y(t) static_assert(__has_trivial_move_constructor(t) && __has_trivial_move_assign(t) && __has_nothrow_copy(t) && __has_nothrow_assign(t) && __has_nothrow_move_assign(t));
Y(A) Y(G) Y(F) Y(int) Y(int[3]) Y(A[2]) Y(int*)
static_assert(__has_nothrow_constructor(A) && __has_nothrow_constructor(int) && __has_nothrow_constructor(A[2]));
static_assert(!__has_nothrow_assign(const int) && !__has_nothrow_assign(const A));
static_assert(__has_nothrow_move_assign(const int) && __has_nothrow_move_assign(const A));
static_assert(__has_nothrow_copy(const A) && __has_trivial_move_constructor(N));
