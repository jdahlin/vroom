#include <stdarg.h>
#include "types.h"
struct float_empty fempty(struct float_empty x) { x.value += 3; return x; }
struct ld_empty ldempty(struct ld_empty x) { x.value += 5; return x; }
union double_empty dempty(union double_empty x) { x.value += 7; return x; }
int empty_va(int n, ...)
{
    va_list ap;
    int bad = 0;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        struct empty e = va_arg(ap, struct empty);
        (void)e;
        if (va_arg(ap, long long) != i + 11) bad = 1;
    }
    va_end(ap);
    return bad;
}
struct packed_hfa packed_empty(struct packed_hfa x)
{
    x.a += 1; x.b += 2; x.c += 3; x.d += 4;
    return x;
}
struct packed_hfa packed_empty_stack(float a, float b, float c, float d,
                                     float e, float f, float g, float h,
                                     struct packed_hfa x)
{
    x.a += a + b; x.b += c + d; x.c += e + f; x.d += g + h;
    return x;
}
/* Both Linux ABIs have exhausted their integer argument registers. The first
 * variadic scalar leaves the overflow cursor eight bytes short of a 16-byte
 * boundary; the empty argument must not align it past the next scalar. */
int empty_after_stack(long long a, long long b, long long c, long long d,
                      long long e, long long f, long long g, long long h, ...)
{
    va_list ap;
    int bad = a != 1 || b != 2 || c != 3 || d != 4 || e != 5 || f != 6 || g != 7 || h != 8;
    va_start(ap, h);
    if (va_arg(ap, long long) != 101) bad = 1;
    (void)va_arg(ap, struct empty);
    if (va_arg(ap, long long) != 102) bad = 1;
    (void)va_arg(ap, struct empty);
    if (va_arg(ap, long long) != 103) bad = 1;
    va_end(ap);
    return bad;
}
