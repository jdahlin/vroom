// [dcl.init.aggr]: __is_constructible for array types (not copy- or
// move-constructible, and not constructible from more arguments than bound).

static_assert(__is_constructible(int[4]));
static_assert(!__is_constructible(int[4], const int (&)[4]));
static_assert(!__is_constructible(int[4], int (&&)[4]));
static_assert(!__is_constructible(int[2], int, int, int));

int main() { return 0; }
