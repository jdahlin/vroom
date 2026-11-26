/* 32-byte vector operators (x86-64: each half its own instruction): lanes of
   8 to 64 bits, shifts, a number in every lane, operands that are the
   result. */
#include <stdio.h>

typedef signed char i8x32 __attribute__((vector_size(32)));
typedef short i16x16 __attribute__((vector_size(32)));
typedef int i32x8 __attribute__((vector_size(32)));
typedef long long i64x4 __attribute__((vector_size(32)));
typedef float f32x8 __attribute__((vector_size(32)));

__attribute__((noinline)) static void bytes(i8x32 *r, const i8x32 *a)
{
    i8x32 x = *a;
    for (int i = 0; i < 3; i++)
        x = (x + *a) ^ (x - 5) & *a;
    *r = x;
}

__attribute__((noinline)) static void words(i16x16 *r, int n)
{
    *r = (*r << n) + (*r >> 3) + *r * *r;
}

__attribute__((noinline)) static void dwords(i32x8 *r, const i32x8 *a)
{
    *r = (*r * *a + *a) ^ (*r - 100);
}

__attribute__((noinline)) static void qwords(i64x4 *r, const i64x4 *a)
{
    *r = (*r + *a) | (*r ^ *a);
}

__attribute__((noinline)) static void reals(f32x8 *r, const f32x8 *a)
{
    *r = *r * *a + *a / (*a + *a);
}

#define SHOW(v, count)                                                         \
    do {                                                                       \
        for (int i = 0; i < count; i++)                                        \
            printf("%lld ", (long long)v[i]);                                  \
        printf("\n");                                                          \
    } while (0)

int main(void)
{
    i8x32 a, ar;
    i16x16 b;
    i32x8 c, d;
    i64x4 e = {1, -2, 3000000000LL, -4}, f = {5, 6, -7, 8000000000LL};
    f32x8 g, h;
    for (int i = 0; i < 32; i++)
        a[i] = (signed char)(i * 11 - 80);
    for (int i = 0; i < 16; i++)
        b[i] = (short)(i * 2111 - 16000);
    for (int i = 0; i < 8; i++) {
        c[i] = i * 100003 - 5;
        d[i] = 7 - i * 13;
        g[i] = (float)i - 3;
        h[i] = (float)i + 1;
    }
    bytes(&ar, &a);
    SHOW(ar, 32);
    words(&b, 2);
    SHOW(b, 16);
    dwords(&c, &d);
    SHOW(c, 8);
    qwords(&e, &f);
    SHOW(e, 4);
    reals(&g, &h);
    SHOW(g, 8);
    return 0;
}
