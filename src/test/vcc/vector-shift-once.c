/* Vector shifts by constants (their mask of all ones is dropped) and loops of
 * one pass through several inlined calls, which are unrolled. */
typedef short v8 __attribute__((vector_size(16)));
int printf(const char *, ...);
#ifdef __x86_64__
#include <emmintrin.h>
static inline __m128i add(__m128i a, __m128i b) { return _mm_add_epi16(a, b); }
static inline __m128i left(__m128i a) { return _mm_slli_epi16(a, 7); }
static inline __m128i right(__m128i a) { return _mm_srai_epi16(a, 3); }
static inline __m128i logical(__m128i a) { return _mm_srli_epi32(a, 5); }
static inline __m128i get(const short *p) { return _mm_loadu_si128((const __m128i *)p); }
static inline void put(short *p, __m128i v) { _mm_storeu_si128((__m128i *)p, v); }
__attribute__((noinline)) static void shifts(short *out, const short *in, int n)
{
    __m128i base = get(in);
    for (int j = 0; j < n; j++) {
        for (int k = 0; k < 1; k++)
            put(out + 8 * j, add(base, add(left(get(in + 8 * j)),
                                           add(right(get(in + 8 * j)),
                                               logical(get(in + 8 * j))))));
        base = add(base, _mm_set1_epi16(8));
    }
}
#else
__attribute__((noinline)) static void shifts(short *out, const short *in, int n)
{
    for (int i = 0; i < 8 * n; i++)
        out[i] = (short)(in[i] << 1);
}
#endif
int main(void)
{
    short in[32], out[32];
    for (int i = 0; i < 32; i++)
        in[i] = (short)(i * 4099 - 32000);
    shifts(out, in, 4);
    for (int i = 0; i < 32; i++)
        printf("%d ", out[i]);
    printf("\n");
    return 0;
}
