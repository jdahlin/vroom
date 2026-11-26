// [expr.type]/3.8: comparing a pointer with a non-pointer (e.g. int) has no
// composite pointer type and is ill-formed, so a requires-expression testing
// `t == u` evaluates to false (e.g. std::equality_comparable_with).

template <class T, class U>
concept eq_comparable = requires(const T &t, const U &u) { t == u; };

static_assert(eq_comparable<int *, int *>);
static_assert(eq_comparable<int *, const int *>);
static_assert(eq_comparable<int *, decltype(nullptr)>);
static_assert(!eq_comparable<int *, int>);
static_assert(!eq_comparable<int, int *>);
static_assert(!eq_comparable<decltype(nullptr), int>);

int main() { return 0; }
