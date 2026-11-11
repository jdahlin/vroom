#include "bfloat.h"
#if defined(__aarch64__) || defined(__x86_64__)
bfloat bfloat_sum(bfloat a, bfloat b) { return a + b; }
bfloat bfloat_many(bfloat a,bfloat b,bfloat c,bfloat d,bfloat e,bfloat f,bfloat g,bfloat h,bfloat i,bfloat j)
{ return a+b+c+d+e+f+g+h+i+j; }
struct h4 bfloat_record(struct h4 x, bfloat y)
{ x.a += y; x.b -= y; x.c *= y; x.d /= y; return x; }
bfloat bfloat_callback(bfloat (*f)(bfloat, bfloat), bfloat x) { return f(x, (bfloat)2); }
bfloat bfloat_varargs(int n, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    bfloat r = 0;
    for (int i=0;i<n;i++) r += __builtin_va_arg(ap, bfloat);
    __builtin_va_end(ap);
    return r;
}
#endif
#if defined(__aarch64__) || defined(__x86_64__)
struct mixed_half bfloat_mixed(struct mixed_half x)
{ x.a += (bfloat)1; x.b += (_Float16)2; x.c += (bfloat)3; return x; }
#endif
