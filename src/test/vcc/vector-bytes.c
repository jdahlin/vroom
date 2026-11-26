/* Lane operations on bytes and words (x86-64: one VIR instruction each; the
   same operands under different operations are different values). */
#include <stdio.h>

typedef unsigned char u8x16 __attribute__((vector_size(16)));
typedef short i16x8 __attribute__((vector_size(16)));

__attribute__((noinline)) static void bytes(u8x16 *r, const u8x16 *a,
                                            const u8x16 *b)
{
    u8x16 x = *a, y = *b;
    u8x16 sum = x + y, difference = x - y, other = y - x;
    *r = (sum ^ difference) + (other & x) - (sum | y);
}

__attribute__((noinline)) static void words(i16x8 *r, const i16x8 *a,
                                            const i16x8 *b)
{
    i16x8 x = *a, y = *b;
    for (int i = 0; i < 5; i++) {
        i16x8 equal = x == y, greater = x > y, less = x < y;
        x = x * y + equal - greater + less;
        y = y + x;
    }
    *r = x ^ y;
}

int main(void)
{
    u8x16 a, b, r;
    i16x8 c, d, s;
    for (int i = 0; i < 16; i++)
        a[i] = (unsigned char)(i * 17 + 3), b[i] = (unsigned char)(250 - i * 9);
    for (int i = 0; i < 8; i++)
        c[i] = (short)(i * 4001 - 13000), d[i] = (short)(900 - i * 300);
    bytes(&r, &a, &b);
    words(&s, &c, &d);
    for (int i = 0; i < 16; i++)
        printf("%d ", r[i]);
    printf("\n");
    for (int i = 0; i < 8; i++)
        printf("%d ", s[i]);
    printf("\n");
    return 0;
}
