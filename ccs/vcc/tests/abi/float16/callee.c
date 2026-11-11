#include "half.h"
#if defined(__aarch64__) || defined(__x86_64__)
half half_sum(half a, half b) { return a + b; }
half half_many(half a,half b,half c,half d,half e,half f,half g,half h,half i,half j)
{ return a+b+c+d+e+f+g+h+i+j; }
struct h4 half_record(struct h4 x, half y)
{ x.a += y; x.b -= y; x.c *= y; x.d /= y; return x; }
half half_callback(half (*f)(half, half), half x) { return f(x, (half)2); }
half half_varargs(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    half r = 0;
    for (int i=0;i<n;i++) r += __builtin_va_arg(ap, half);
    __builtin_va_end(ap);
    return r;
}
#endif
