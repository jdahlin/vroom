#include "half.h"
#if defined(__aarch64__) || defined(__x86_64__)
static half callback(half a, half b) { return a * b; }
#endif
int main(void)
{
#if defined(__aarch64__) || defined(__x86_64__)
    volatile half x = (half)1.5;
    struct h4 a = {1,2,3,4}, b = half_record(a, (half)2);
    if (half_sum(x, (half)2.25) != (half)3.75) return 1;
    if (half_many(1,2,3,4,5,6,7,8,9,10) != 55) return 2;
    if (b.a != 3 || b.b != 0 || b.c != 6 || b.d != 2) return 3;
    if (half_callback(callback, x) != 3) return 4;
    if (half_varargs(3, (half)1, (half)2, (half)3) != 6) return 5;
#endif
    return 0;
}
