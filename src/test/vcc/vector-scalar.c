/* A vector and one number: shifts by a variable and a constant count, and the
   number in every lane. */
#include <stdio.h>

typedef signed char i8x16 __attribute__((vector_size(16)));
typedef short i16x8 __attribute__((vector_size(16)));
typedef unsigned short u16x8 __attribute__((vector_size(16)));
typedef int i32x4 __attribute__((vector_size(16)));
typedef long long i64x2 __attribute__((vector_size(16)));

#define SHIFTS(name, type)                                                     \
    __attribute__((noinline)) static void name(type *r, const type *a, int n)  \
    {                                                                          \
        type x = *a;                                                           \
        *r = (x << n) ^ (x >> n) ^ (x >> 3) ^ (x << 1);                        \
    }
SHIFTS(shift16, i16x8)
SHIFTS(shiftu16, u16x8)
SHIFTS(shift32, i32x4)

#define ADDS(name, type, scalar)                                               \
    __attribute__((noinline)) static void name(type *r, const type *a,         \
                                               scalar n)                       \
    {                                                                          \
        type x = *a;                                                           \
        *r = ((x + n) ^ (x - n)) + (x & n) + (x | 5);                          \
    }
ADDS(add8, i8x16, signed char)
ADDS(add16, i16x8, short)
ADDS(add32, i32x4, int)
ADDS(add64, i64x2, long long)

#define SHOW(v, count)                                                         \
    do {                                                                       \
        for (int i = 0; i < count; i++)                                        \
            printf("%lld ", (long long)v[i]);                                  \
        printf("\n");                                                          \
    } while (0)

int main(void)
{
    i8x16 a, ar;
    i16x8 b = {-30000, -3, 0, 1, 2, 300, 12345, 32000}, br;
    u16x8 c = {65535, 3, 0, 1, 2, 300, 12345, 32768}, cr;
    i32x4 d = {-100000, -1, 7, 2000000000}, dr;
    i64x2 f = {-5000000000LL, 9000000000LL}, fr;
    for (int i = 0; i < 16; i++)
        a[i] = (signed char)(i * 23 - 90);
    for (int n = 0; n < 16; n += 5) {
        shift16(&br, &b, n);
        SHOW(br, 8);
        shiftu16(&cr, &c, n);
        SHOW(cr, 8);
        shift32(&dr, &d, n);
        SHOW(dr, 4);
    }
    add8(&ar, &a, -7);
    SHOW(ar, 16);
    add16(&br, &b, -1234);
    SHOW(br, 8);
    add32(&dr, &d, 100000);
    SHOW(dr, 4);
    add64(&fr, &f, -123456789012LL);
    SHOW(fr, 2);
    return 0;
}
