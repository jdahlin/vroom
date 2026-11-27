/* 256-bit vectors set from different elements (built in registers, constants
 * and runtime values, 32 and 64 bits) and permutes by a vector of indices;
 * results the same as clang's.  Runs only on a CPU with AVX2. */
int printf(const char *, ...);
#ifdef __x86_64__
#include <immintrin.h>
__attribute__((noinline, target("avx2"))) static void
run(__m256i *o, const int *p, const long long *q, const __m256i *a)
{
    __m256i order = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);
    __m256i mixed = _mm256_set_epi32(p[0], 11, p[1], p[2], -5, p[3], p[4], 9);
    __m256i wide = _mm256_set_epi64x(q[0], 3, q[1], -q[2]);
    __m256i half = _mm256_set_epi64x(q[3], q[3], p[5], p[5]);

    o[0] = order;
    o[1] = mixed;
    o[2] = wide;
    o[3] = half;
    o[4] = _mm256_permutevar8x32_epi32(a[0], order);
    o[5] = _mm256_permutevar8x32_epi32(a[1], mixed);
    o[6] = _mm256_castps_si256(
        _mm256_permutevar8x32_ps(_mm256_castsi256_ps(a[0]), a[1]));
    o[7] = _mm256_permutevar8x32_epi32(mixed, a[2]);
}
int main(void)
{
    static __m256i o[8], a[3];
    static int p[8] = { 100, -200, 300000, 0x7fffffff, -1, 42, 7, 8 };
    static long long q[4] = { 0x123456789abcdefLL, -9, 1LL << 62, 77 };
    int *w = (int *)a;
    unsigned x, b, c, d, h = 0;

    __asm__("cpuid" : "=a"(x), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    for (int i = 0; i < 24; i++)
        w[i] = i * 0x9e3779b1 + 3;
    if (b >> 5 & 1)
        for (int k = 0; k < 10; k++) {
            p[k % 8] += k * 12345;
            run(o, p, q, a);
            for (int i = 0; i < 64; i++)
                h = h * 31 + ((unsigned *)o)[i];
            w[k] ^= (int)h;
        }
    printf("%08x\n", h);
    return 0;
}
#else
int main(void)
{
    printf("not x86-64\n");
    return 0;
}
#endif
