/* abs, labs and llabs, and their __builtin_ names, are computed in place, as
 * clang: same values, arguments evaluated once, constants still constant. */
#include <stdio.h>
#include <stdlib.h>
static int global = __builtin_abs(-3);
static long wide = __builtin_labs(-4000000000L);
static int calls;
static int next(void) { return -++calls; }
__attribute__((noinline)) static long values(int a, long b, long long c)
{
    return abs(a) + __builtin_abs(a - 1) * 3 + labs(b) * 5 +
           __builtin_labs(b) * 7 + llabs(c) * 11 + __builtin_llabs(c) * 13;
}
int main(void)
{
    int table[__builtin_abs(-2)] = {1, 2};
    _Static_assert(__builtin_abs(-5) == 5, "constant");
    for (int i = -3; i <= 3; i++)
        printf("%ld\n", values(i, i * 100000L, i * 5000000000LL));
    int x = abs(next()) + __builtin_abs(next());
    printf("%d %d %d %ld %d\n", x, calls, global, wide, table[1]);
    return 0;
}
