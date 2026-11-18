// constexpr functions called at run time: [dcl.constexpr]/1 (implicitly
// inline), [expr.const]/5.2 (their invocations are the evaluator's, which
// answers unsupported until it is written: no constant use here)
#include <stdio.h>

constexpr int square(int x) { return x * x; }
constexpr int sum_to(int n)
{
    int s = 0;
    for (int i = 1; i <= n; i++)
        s += i;
    return s;
}
static constexpr long twice(long v) { return 2 * v; }
namespace n {
constexpr int cube(int x) { return x * square(x); }
}

// (a namespace-scope `int g = square(7);` is constant-initialized,
// [basic.start.static]/2: unsupported until the evaluator decides it)

int main()
{
    int k = 6;
    printf("%d %d %d %ld %d\n", square(k), sum_to(k), n::cube(3), twice(21),
           square(sum_to(3)));
    return 0;
}
