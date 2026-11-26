/* SSE2 shifts by constant and run-time counts, in and past the lane's width, and the
   pair multiplies. */
#ifdef __x86_64__
#include <emmintrin.h>
int printf(const char *, ...);

static void show(const char *name, int n, __m128i v)
{
    unsigned u[4];
    _mm_storeu_si128((__m128i *)u, v);
    printf("%s %d %08x %08x %08x %08x\n", name, n, u[0], u[1], u[2], u[3]);
}

#define CONSTANT(n)                                                            \
    show("psllw", n, _mm_slli_epi16(a, n));                                    \
    show("pslld", n, _mm_slli_epi32(a, n));                                    \
    show("psllq", n, _mm_slli_epi64(a, n));                                    \
    show("psrlw", n, _mm_srli_epi16(a, n));                                    \
    show("psrld", n, _mm_srli_epi32(a, n));                                    \
    show("psrlq", n, _mm_srli_epi64(a, n));                                    \
    show("psraw", n, _mm_srai_epi16(a, n));                                    \
    show("psrad", n, _mm_srai_epi32(a, n))

__attribute__((noinline)) static void constants(__m128i a)
{
    CONSTANT(0);
    CONSTANT(1);
    CONSTANT(8);
    CONSTANT(15);
    CONSTANT(16);
    CONSTANT(31);
    CONSTANT(32);
    CONSTANT(63);
    CONSTANT(64);
    CONSTANT(255);
    CONSTANT(256);
    CONSTANT(-1);
}
__attribute__((noinline)) static void variable(__m128i a, int n)
{
    CONSTANT(n);
}
__attribute__((noinline)) static void multiply(__m128i a, __m128i b)
{
    show("pmaddwd", 0, _mm_madd_epi16(a, b));
    show("pmulhw", 0, _mm_mulhi_epi16(a, b));
}

int main(void)
{
    __m128i a = _mm_set_epi32(0x80008000, 0x7fff8001, 0x12345678, 0xfedcba98);
    __m128i m = _mm_set1_epi16((short)0x8000);
    constants(a);
    for (int n = -1; n <= 70; n += 7)
        variable(a, n);
    multiply(a, a);
    multiply(a, _mm_set1_epi16(-3));
    multiply(m, m);
    return 0;
}
#else
int main(void)
{
    return 0;
}
#endif
