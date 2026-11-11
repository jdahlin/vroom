#include "args.h"
double many(EACH(PARAM))
{
    double parts[] = { EACH(TERM) }, sum = 0;
    for (int i = 0; i < 24; i++) sum += parts[i];
    return sum;
}
struct result record_many(EACH(PARAM))
{
    double parts[] = { EACH(TERM) }, sum = 0;
    for (int i = 0; i < 24; i++) sum += parts[i];
    return (struct result){(long)sum, (long)sum + 1, (long)sum + 2};
}
double variadic_many(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    double sum = 0;
    for (int i = 0; i < n; i++) {
        long a = __builtin_va_arg(ap, long);
        double b = __builtin_va_arg(ap, double);
        struct pair p = __builtin_va_arg(ap, struct pair);
        sum += a + b + p.a + p.b;
    }
    __builtin_va_end(ap);
    return sum;
}

double wide_many(WIDE(PARAM))
{
    double parts[] = { WIDE(TERM) }, sum = 0;
    for (int i = 0; i < 97; i++) sum += parts[i];
    return sum;
}
