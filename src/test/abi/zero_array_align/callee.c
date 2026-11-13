#include "types.h"
struct item item_change(struct item a, int n, struct item b)
{ a.a += n + b.a; a.b = !a.b; return a; }
int item_varargs(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    int sum = 0;
    for (int i = 0; i < n; i++) {
        struct item a = __builtin_va_arg(ap, struct item);
        sum += a.a + a.b;
    }
    __builtin_va_end(ap);
    return sum;
}
