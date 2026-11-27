/* The sign-bit masks of a vector (movmskps, movmskpd, pmovmskb). */
#ifdef __x86_64__
#include <emmintrin.h>
int printf(const char *, ...);

__attribute__((noinline)) static void masks(const __m128i *p, int n)
{
    unsigned long long sum = 0;
    for (int i = 0; i < n; i++) {
        __m128i v = _mm_loadu_si128(p + i);
        int ps = _mm_movemask_ps(_mm_castsi128_ps(v));
        int pd = _mm_movemask_pd(_mm_castsi128_pd(v));
        int b = _mm_movemask_epi8(v);
        unsigned long long wide = (unsigned)_mm_movemask_epi8(v);
        int nonzero = _mm_movemask_ps(
            _mm_castsi128_ps(_mm_cmpgt_epi32(v, _mm_setzero_si128())));
        printf("%d %x %x %x %x\n", i, ps, pd, b, nonzero);
        sum = sum * 31 + wide + (unsigned)ps;
    }
    printf("%llx\n", sum);
}

int main(void)
{
    __m128i p[8];
    for (int i = 0; i < 8; i++)
        p[i] = _mm_set_epi32(i * 0x91234567u, ~(i * 0x1010101u), i - 4,
                             i % 3 * 0x80808080u);
    masks(p, 8);
    return 0;
}
#else
int main(void)
{
    return 0;
}
#endif
