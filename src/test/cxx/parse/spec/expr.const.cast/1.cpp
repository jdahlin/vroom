// [expr.const.cast] Example 1 (paragraph 3), docs/cxx-spec/expr/expr.const.cast.md
// needs: P3
// status: annotated

typedef int *A[3];                  // array of 3 pointer to int
typedef const int *const CA[3];     // array of 3 const pointer to const int

CA &&r = A{};                       // OK, reference binds to temporary array object
                                    // after qualification conversion to type CA
A &&r1 = const_cast<A>(CA{});       // error: temporary array decayed to pointer // semantic
A &&r2 = const_cast<A&&>(CA{});     // OK
