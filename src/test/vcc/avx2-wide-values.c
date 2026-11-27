/* 256-bit values kept in registers: loops carrying them, lane operators of
 * every width, AVX2 builtins that work on both halves, copies, spills across
 * calls, results the same as clang's. */
int printf(const char *, ...);
#ifdef __x86_64__
#include <immintrin.h>
typedef short v16s __attribute__((vector_size(32)));
typedef int v8s __attribute__((vector_size(32)));
typedef long long v4d __attribute__((vector_size(32)));
typedef char v32c __attribute__((vector_size(32)));
__attribute__((noinline)) static void opaque(int *p) { (*p)++; }
__attribute__((noinline, target("avx2"))) static void lanes(void *o, const v8s *a, int n)
{
    v8s sum = a[0], mix = a[1];
    v16s words = (v16s)a[2];
    v32c bytes = (v32c)a[3];
    int calls = 0;
    for (int i = 1; i < n; i++) {
        sum += a[i];
        mix = (mix ^ a[i]) | (sum & a[0]);
        words = words - (v16s)a[i] + (v16s)sum;
        bytes = bytes + (v32c)mix;
        if (i == 3)
            opaque(&calls);
    }
    ((v8s *)o)[0] = sum;
    ((v8s *)o)[1] = mix;
    ((v16s *)o)[2] = words;
    ((v32c *)o)[3] = bytes;
    ((v4d *)o)[4] = (v4d)sum + (v4d)mix;
}
__attribute__((noinline, target("avx2"))) static void builtins(__m256i *o, const __m256i *a, int n)
{
    __m256i s = a[0], t = a[1], u = a[2];
    for (int i = 1; i < n; i++) {
        s = _mm256_adds_epi16(s, a[i]);
        t = _mm256_maddubs_epi16(a[i], t);
        t = _mm256_madd_epi16(t, _mm256_set1_epi16(1));
        u = _mm256_add_epi32(u, _mm256_mulhi_epi16(s, a[i]));
        u = _mm256_max_epi16(u, _mm256_min_epi16(t, _mm256_srai_epi16(s, 3)));
        u = _mm256_packs_epi16(u, _mm256_slli_epi16(t, 5));
    }
    o[0] = s;
    o[1] = t;
    o[2] = u;
}
/* the set1 intrinsics: one value in every lane, of every width, from values
   with high bits set */
__attribute__((noinline, target("avx2"))) static void splats(__m256i *o, int x,
                                                             long long y)
{
    o[0] = _mm256_set1_epi8((char)x);
    o[1] = _mm256_set1_epi16((short)x);
    o[2] = _mm256_set1_epi32(x);
    o[3] = _mm256_set1_epi64x(y);
    o[4] = _mm256_add_epi32(_mm256_set1_epi32(-1), _mm256_set1_epi16(1));
}
__attribute__((noinline, target("avx2"))) static int signs(const __m256i *a)
{
    return _mm256_movemask_ps(_mm256_castsi256_ps(a[0])) * 256 +
           _mm256_movemask_ps(_mm256_castsi256_ps(
               _mm256_cmpgt_epi32(a[1], _mm256_setzero_si256())));
}
#endif
int main(void)
{
#ifdef __x86_64__
    static __attribute__((aligned(32))) int in[8][8];
    static __attribute__((aligned(32))) int out[5][8];
    for (int i = 0; i < 64; i++)
        in[i / 8][i % 8] = i * 0x1234567 ^ (i << 27);
    for (int n = 1; n <= 8; n += 7) {
        lanes(out, (const v8s *)in, n);
        for (int i = 0; i < 40; i++)
            printf("%08x ", out[i / 8][i % 8]);
        printf("\n");
        for (int i = 0; i + 1 < 8; i++)
            printf("%x ", signs((const __m256i *)in + i));
        splats((__m256i *)out, n * 0x12345 - 0x80000, -0x123456789LL * n);
        for (int i = 0; i < 40; i++)
            printf("%08x ", out[i / 8][i % 8]);
        printf("\n");
        builtins((__m256i *)out, (const __m256i *)in, n);
        for (int i = 0; i < 24; i++)
            printf("%08x ", out[i / 8][i % 8]);
        printf("\n");
    }
#endif
    return 0;
}
