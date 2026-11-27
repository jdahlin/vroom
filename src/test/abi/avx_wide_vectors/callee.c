/* vcc-flags: -mavx2 */
#include <stdarg.h>
#include "wide.h"

v16si add(v16si a, int by, v16si b)
{
    return a + by * b;
}

v16df twice(v16df a)
{
    return a + a;
}

v32di thrice(v32di a)
{
    return a + a + a;
}

v2ti swapped(v2ti a)
{
    v2ti r = {a[1], a[0]};
    return r;
}

struct box boxed(struct box x)
{
    x.v += 3;
    return x;
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
