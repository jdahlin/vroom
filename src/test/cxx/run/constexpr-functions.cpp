// constexpr functions: [dcl.constexpr]/1 (implicitly inline), called at
// run time and evaluated as constant expressions ([expr.const]/5): in
// static_assert, array bounds, enumerators, constexpr variables and the
// constant initialization of a variable with static storage duration
// ([basic.start.static]/2, emitted as data)
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
constexpr int fib(int n)
{
    if (n < 2)
        return n;
    return fib(n - 1) + fib(n - 2);
}
constexpr int collatz(unsigned n)
{
    int steps = 0;
    while (n != 1) {
        if (n % 2)
            n = 3 * n + 1;
        else
            n /= 2;
        ++steps;
    }
    return steps;
}
constexpr int odd_sum(int n)
{
    int s = 0, i = 0;
    do {
        i++;
        if (i % 2 == 0)
            continue;
        if (i > 99)
            break;
        s += i;
    } while (i < n);
    return s;
}
constexpr unsigned char wrap(unsigned char c)
{
    c += 200;
    return c;
}
constexpr double half(double d) { return d / 2; }

static_assert(square(7) == 49, "");
static_assert(fib(15) == 610, "");
static_assert(collatz(27) == 111, "");
static_assert(odd_sum(10) == 25, "");
static_assert(wrap(100) == 44, "");
static_assert(half(3.0) == 1.5, "");

constexpr int b = square(4);
int g = square(7) + b;
static long t = twice(fib(20));
double h = half(5.0);
int bound[sum_to(4)];
enum { E = n::cube(3) };

int main()
{
    int k = 6;
    printf("%d %d %d %ld %d\n", square(k), sum_to(k), n::cube(3), twice(21),
           square(sum_to(3)));
    printf("%d %d %ld %g %d %d %d\n", g, b, t, h, (int)sizeof bound,
           (int)E, collatz(k));
    return 0;
}
