/* vcc-flags: -mavx512f */
#include <stdarg.h>
#include "avx512.h"

v16si add(v16si a, v16si b)
{
    return a + b;
}

v16sf mix(int i, v16sf a, double d, v8df b, long n, v64qi c)
{
    for (int k = 0; k < 16; k++)
        a[k] = a[k] + (float)(i + d + b[k % 8] + n + c[k * 4]);
    return a;
}

v16si many(v16si a, v16si b, v16si c, v16si d, v16si e, v16si f, v16si g,
           v16si h, int i, v16si j, v2di k, v8si m, v16si l)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + i +
           9 * j + (int)(k[0] + k[1]) + m[7] + 10 * l;
}

struct box boxed(struct box x, int by)
{
    x.v *= by;
    return x;
}

struct arrayed arrayed(struct arrayed x)
{
    x.v[0] += 5;
    return x;
}

struct halves halves(struct halves x)
{
    struct halves y = {x.b, x.a};
    return y;
}

union overlaid overlaid(union overlaid x)
{
    x.v += 9;
    return x;
}

v32si twice(v32si a)
{
    return a + a;
}

v64si thrice(v64si a)
{
    return a + a + a;
}

int variadic(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    int sum = 0;
    for (int i = 0; i < n; i++) {
        v16si v = va_arg(ap, v16si);
        for (int k = 0; k < 16; k++)
            sum += v[k] * (i + 1);
    }
    va_end(ap);
    return sum;
}

v16si named_variadic(v16si first, int n, ...)
{
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++)
        first += va_arg(ap, int);
    va_end(ap);
    return first;
}

v16si callback(v16si (*fn)(v16si, int), v16si x)
{
    return fn(fn(x, 3), 5) + x;
}

__attribute__((target("avx512bw"))) v64qi bytes(v64qi a, v64qi b)
{
    return a + b;
}
