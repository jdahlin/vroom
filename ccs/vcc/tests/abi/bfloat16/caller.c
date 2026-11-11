#include "bfloat.h"
#if defined(__aarch64__) || defined(__x86_64__)
static bfloat callback(bfloat a, bfloat b) { return a * b; }
#endif
int main(void)
{
#if defined(__aarch64__) || defined(__x86_64__)
    struct mixed_half m = {1,2,3}, r = bfloat_mixed(m);
    if (r.a != 2 || r.b != 4 || r.c != 6) return 6;
    volatile bfloat x = (bfloat)1.5;
    struct h4 a = {1,2,3,4}, b = bfloat_record(a, (bfloat)2);
    if (bfloat_sum(x, (bfloat)2.25) != (bfloat)3.75) return 1;
    if (bfloat_many(1,2,3,4,5,6,7,8,9,10) != 55) return 2;
    if (b.a != 3 || b.b != 0 || b.c != 6 || b.d != 2) return 3;
    if (bfloat_callback(callback, x) != 3) return 4;
    if (bfloat_varargs(3, (bfloat)1, (bfloat)2, (bfloat)3) != 6) return 5;
#endif
    return 0;
}
