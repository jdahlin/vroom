// __array_extent(T, N) (clang's [meta.unary.prop.query] extent): clang++'s values
typedef int A3[3];
typedef int AA[2][5][7];
typedef int U[];
typedef int UA[][4];
static_assert(__array_extent(int, 0) == 0);
static_assert(__array_extent(A3, 0) == 3);
static_assert(__array_extent(A3, 1) == 0);
static_assert(__array_extent(AA, 0) == 2);
static_assert(__array_extent(AA, 1) == 5);
static_assert(__array_extent(AA, 2) == 7);
static_assert(__array_extent(AA, 3) == 0);
static_assert(__array_extent(U, 0) == 0);
static_assert(__array_extent(UA, 0) == 0);
static_assert(__array_extent(UA, 1) == 4);
static_assert(__array_extent(const A3, 0) == 3);
static_assert(__array_extent(A3 *, 0) == 0);
