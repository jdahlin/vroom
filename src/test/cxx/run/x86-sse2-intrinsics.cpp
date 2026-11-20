/* [ext:gnu_vector_size] clang's <emmintrin.h> on x86-64: the intrinsics a
   byte search (the lexer's simd-search.h) uses, which are vector operations,
   compound literals and two builtins (__builtin_ia32_pmovmskb128,
   __builtin_elementwise_min).  The functions of the headers nothing uses are
   left out.  Other targets compile the portable part only. */
#if defined(__SSE2__)
#include <emmintrin.h>

static unsigned first_special(const char *p)
{
    __m128i v = _mm_loadu_si128((const __m128i *)p);
    __m128i zero = _mm_cmpeq_epi8(v, _mm_set1_epi8(0));
    __m128i nl = _mm_cmpeq_epi8(v, _mm_set1_epi8('\n'));
    unsigned mask = (unsigned)_mm_movemask_epi8(_mm_or_si128(zero, nl));
    return mask;
}

static int clamp_sum(const unsigned char *p)
{
    __m128i v = _mm_loadu_si128((const __m128i *)p);
    __m128i lo = _mm_min_epu8(v, _mm_set1_epi8(100));
    __m128i over = _mm_sub_epi8(v, lo);
    unsigned char out[16], rest[16];
    _mm_storeu_si128((__m128i *)out, lo);
    _mm_storeu_si128((__m128i *)rest, over);
    int sum = 0;
    for (int i = 0; i < 16; i++)
        sum += out[i] * 3 + rest[i];
    return sum;
}

static int lanes32()
{
    __m128i a = _mm_set_epi32(4, 3, 2, 1), b = _mm_set1_epi32(10);
    __m128i s = _mm_add_epi32(a, b);
    int out[4];
    _mm_storeu_si128((__m128i *)out, s);
    return out[0] * 1000 + out[3];
}
#endif

int main()
{
#if defined(__SSE2__)
    char text[17] = "ab\ncdefghi\0klmno";
    unsigned char bytes[16] = {1, 50, 99, 100, 101, 150, 200, 255,
                               0, 7, 100, 99, 250, 3, 100, 101};
    if (first_special(text) != ((1u << 2) | (1u << 10)))
        return 1;
    if (clamp_sum(bytes) != 3934)
        return 2;
    if (lanes32() != 11014)
        return 3;
#endif
    return 0;
}
