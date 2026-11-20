/* [ext:gnu_vector_size] more of <emmintrin.h> on x86-64: saturating adds
   and subtracts, shifts (by constants and by counts at and past the lane
   width), shuffles, byte shifts, conversions, lane access and streaming
   stores.  The output is compared with clang's. */
#if defined(__SSE2__)
#include <emmintrin.h>
#include <stdio.h>
#include <string.h>

static void show(const char *what, __m128i v)
{
    unsigned char b[16];
    memcpy(b, &v, 16);
    printf("%s:", what);
    for (int i = 0; i < 16; i++)
        printf(" %02x", b[i]);
    printf("\n");
}

static __m128i bytes(unsigned seed, unsigned step)
{
    unsigned char b[16];
    for (int i = 0; i < 16; i++)
        b[i] = (unsigned char)(seed + step * i * 37 + i * i);
    __m128i v;
    memcpy(&v, b, 16);
    return v;
}

static void shifts(__m128i a, int n)
{
    show("sll16", _mm_slli_epi16(a, n));
    show("sll32", _mm_slli_epi32(a, n));
    show("sll64", _mm_slli_epi64(a, n));
    show("srl16", _mm_srli_epi16(a, n));
    show("srl32", _mm_srli_epi32(a, n));
    show("srl64", _mm_srli_epi64(a, n));
    show("sra16", _mm_srai_epi16(a, n));
    show("sra32", _mm_srai_epi32(a, n));
}
#endif

int main()
{
#if defined(__SSE2__)
    __m128i a = bytes(3, 5), b = bytes(200, 11), c = bytes(127, 1);
    show("adds8", _mm_adds_epi8(a, b));
    show("adds8c", _mm_adds_epi8(c, c));
    show("addsu8", _mm_adds_epu8(a, b));
    show("subs8", _mm_subs_epi8(a, b));
    show("subsu8", _mm_subs_epu8(a, b));
    show("adds16", _mm_adds_epi16(a, c));
    show("addsu16", _mm_adds_epu16(a, c));
    show("subs16", _mm_subs_epi16(b, a));
    show("subsu16", _mm_subs_epu16(c, b));
    static const int counts[] = {0, 1, 7, 15, 16, 31, 32, 63, 64, 100, -1};
    for (int i = 0; i < 11; i++)
        shifts(a, counts[i]);
    show("shufd", _mm_shuffle_epi32(a, 0x1b));
    show("shufd2", _mm_shuffle_epi32(b, 0xe4));
    show("shuflo", _mm_shufflelo_epi16(a, 0x39));
    show("shufhi", _mm_shufflehi_epi16(b, 0x93));
    show("bsl3", _mm_slli_si128(a, 3));
    show("bsr5", _mm_srli_si128(a, 5));
    show("bsl16", _mm_slli_si128(a, 16));
    show("bsr0", _mm_srli_si128(b, 0));
    float f[4] = {1.9f, -2.9f, 1e9f, 0.4f};
    double d[2] = {7.9, -3.2};
    show("cvttps", _mm_cvttps_epi32(_mm_loadu_ps(f)));
    show("cvttpd", _mm_cvttpd_epi32(_mm_loadu_pd(d)));
    printf("ext %d %d\n", _mm_extract_epi16(a, 2), _mm_extract_epi16(b, 7));
    show("ins", _mm_insert_epi16(a, 0x1234, 5));
    __m128i sink[2];
    _mm_stream_si128(&sink[0], a);
    _mm_store_si128(&sink[1], b);
    show("stream", sink[0]);
    show("store", sink[1]);
    show("undef", _mm_setzero_si128());
#endif
    return 0;
}
