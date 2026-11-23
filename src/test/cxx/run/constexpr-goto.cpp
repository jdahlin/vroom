/* goto is not a constant expression for clang's evaluator in any language
   version: a constexpr function with one is valid (C++23) but evaluating
   it in a constant expression is not; at run time it works. */
/* cxx-run-flags: -std=c++23 */
#include <stdio.h>

constexpr int counted(int n)
{
    int r = 0;
again:
    if (n > 0) {
        r += n;
        --n;
        goto again;
    }
    return r;
}

constexpr int plain(int n) { return n * 2; }

static_assert(plain(4) == 8);

int main()
{
    printf("%d %d\n", counted(3), plain(4));
    return 0;
}
