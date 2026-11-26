/* vcc-flags: -mavx2 */
#include <stdarg.h>
#include "avx.h"

union overlaid overlaid(union overlaid x)
{
    x.a[1] += 1000;
    return x;
}

struct padded padded(struct padded x)
{
    x.v += 7;
    return x;
}

v8si add(v8si a, v8si b)
{
    return a + b;
}

v8sf mix(int i, v8sf a, double d, v4df b, long n, v32qi c)
{
    for (int k = 0; k < 8; k++)
        a[k] = a[k] + (float)(i + d + b[k % 4] + n + c[k * 4]);
    return a;
}

v8si many(v8si a, v8si b, v8si c, v8si d, v8si e, v8si f, v8si g, v8si h,
          int i, v8si j, v2di k, v8si l)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + i +
           9 * j + (int)(k[0] + k[1]) + 10 * l;
}

struct box boxed(struct box x, int by)
{
    x.v *= by;
    return x;
}

struct pair paired(struct pair x)
{
    struct pair y = {x.b, x.a};
    return y;
}

int variadic(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    int sum = 0;
    for (int i = 0; i < n; i++) {
        v8si v = va_arg(ap, v8si);
        for (int k = 0; k < 8; k++)
            sum += v[k] * (i + 1);
    }
    va_end(ap);
    return sum;
}

v8si named_variadic(v8si first, int n, ...)
{
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++)
        first += va_arg(ap, int);
    va_end(ap);
    return first;
}

v8si callback(v8si (*fn)(v8si, int), v8si x)
{
    return fn(fn(x, 3), 5) + x;
}
