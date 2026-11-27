/* -O2 (VIR) inlines an external function of the same unit, as clang; weak and
 * noinline ones stay calls, and an inlined copy behaves as the call did. */
#include <stdarg.h>
#include <stdio.h>
struct pair {
    int a, b;
};
int counter;
int next(void) { return ++counter; }
struct pair make(int a, int b)
{
    struct pair p = {a, b};
    return p;
}
int sum(struct pair p) { return p.a + p.b; }
int local_static(void)
{
    static int n;
    return ++n;
}
int twice(int *p)
{
    int local[4] = {*p, *p + 1};
    return local[0] + local[1] + local[3];
}
__attribute__((weak)) int overridable(void) { return 3; }
__attribute__((noinline)) int kept(int x) { return x * 2; }
int factorial(int n) { return n < 2 ? 1 : n * factorial(n - 1); }
int main(void)
{
    int x = 5;
    int a = next(), b = next();
    printf("%d %d %d\n", a, b, counter);
    printf("%d %d\n", sum(make(2, 3)), twice(&x));
    int c = local_static(), d = local_static(), (*f)(void) = local_static;
    printf("%d %d %d\n", c, d, f());
    printf("%d %d %d\n", overridable(), kept(7), factorial(5));
    return 0;
}
