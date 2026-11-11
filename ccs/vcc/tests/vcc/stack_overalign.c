#include <stdarg.h>

typedef unsigned long word;
struct __attribute__((aligned(64))) block { word x[8]; };

static __attribute__((noinline)) int aligned(int depth, int n, int a, int b,
                                          int c, int d, int g, int h)
{
    _Alignas(64) volatile char fixed[73];
    _Alignas(256) volatile char variable[n];
    if ((word)fixed % 64 || (word)variable % 256) return 1;
    fixed[0] = depth; fixed[72] = g;
    variable[0] = h; variable[n - 1] = n;
    if (depth && aligned(depth - 1, n + 1, a, b, c, d, g, h)) return 2;
    if (fixed[0] != depth || fixed[72] != g || variable[0] != h || variable[n-1] != n)
        return 3;
    return a + b + c + d == 10 ? 0 : 4;
}

static __attribute__((noinline)) int varargs(int n, ...)
{
    _Alignas(128) volatile char data[9];
    va_list ap;
    int total = 0;
    data[0] = 17;
    va_start(ap, n);
    for (int i = 0; i < n; i++) total += va_arg(ap, int);
    va_end(ap);
    return (word)data % 128 || data[0] != 17 || total != 45;
}

static __attribute__((noinline)) word byvalue(struct block b, word v)
{
    _Alignas(256) char extra[17];
    if ((word)extra % 256 || (word)&b % _Alignof(struct block)) return 999;
    return b.x[0] + b.x[7] + v;
}

int main(void)
{
    struct block b = { { 7, 0, 0, 0, 0, 0, 0, 11 } };
    if (aligned(3, 21, 1, 2, 3, 4, 5, 6)) return 1;
    if (varargs(9, 1, 2, 3, 4, 5, 6, 7, 8, 9)) return 2;
    if (byvalue(b, 13) != 31) return 3;
    return 0;
}
