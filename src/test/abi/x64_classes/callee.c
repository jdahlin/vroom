#include "classes.h"
v1ll vector_identity(v1ll x) { return x; }
struct ld ld_identity(struct ld x) { x.x += 0.5L; return x; }
struct padded padded_sum(struct padded x, int n, struct padded y)
{ x.x += n + y.x; return x; }
int padded_va(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    int sum = 0;
    for (int i=0; i<n; i++) sum += __builtin_va_arg(ap, struct padded).x;
    __builtin_va_end(ap);
    return sum;
}
struct odd odd_identity(struct odd x) { x.b += x.a; return x; }
#ifdef __x86_64__
v16f wide_identity(v16f x) { return x; }
#endif
struct under under_sum(struct under x, int n, struct under y)
{ x.a += y.a; x.b += y.b; x.c += y.c + n; return x; }
