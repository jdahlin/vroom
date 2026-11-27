/* Vector loads used once by the next operation become its memory operand:
 * both operand orders, indexed addresses, 256 and 512 bits, vpdpbusd;
 * results the same as clang's.  Each half runs only on a CPU that has it. */
int printf(const char *, ...);
#ifdef __x86_64__
#include <immintrin.h>
#define OPS(P, V)                                                              \
    for (long i = 0; i < k; i++) {                                             \
        V x = acc[i];                                                          \
        x = P##_add_epi16(x, P##_sub_epi16(a[i], b[i]));                       \
        x = P##_sub_epi16(x, b[i + 1]);                                        \
        x = P##_add_epi16(a[i + 1], x); /* (they trade places) */              \
        x = P##_sub_epi16(a[i], x);     /* (they may not) */                   \
        x = P##_max_epi16(x, b[i]);                                            \
        x = P##_maddubs_epi16(x, a[i]);                                        \
        x = P##_maddubs_epi16(a[i], x);                                        \
        x = P##_packs_epi32(x, b[i]);                                          \
        acc[i] = x;                                                            \
    }
__attribute__((noinline, target("avx2"))) static void
two(__m256i *acc, const __m256i *a, const __m256i *b, long k)
{
    OPS(_mm256, __m256i)
}
__attribute__((noinline, target("avx512f,avx512bw"))) static void
five(__m512i *acc, const __m512i *a, const __m512i *b, long k)
{
    OPS(_mm512, __m512i)
}
__attribute__((noinline, target("avx512f,avx512bw,avx512vnni"))) static void
dot(__m512i *acc, const __m512i *a, const __m512i *b, long k)
{
    for (long i = 0; i < k; i++) {
        acc[0] = _mm512_dpbusd_epi32(acc[0], a[i], b[i]);
        acc[1] = _mm512_dpbusd_epi32(acc[1], b[i + 1], a[i]);
    }
}
static void cpuid7(unsigned *b, unsigned *c)
{
    unsigned a, d;
    __asm__("cpuid" : "=a"(a), "=b"(*b), "=c"(*c), "=d"(d) : "a"(7), "c"(0));
}
static unsigned hash(unsigned h, const void *p, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        h = h * 31 + ((const unsigned char *)p)[i];
    return h;
}
int main(void)
{
    static __m512i a[10], b[10], acc[10];
    unsigned char *pa = (unsigned char *)a, *pb = (unsigned char *)b;
    unsigned ebx, ecx, h = 0;

    for (unsigned i = 0; i < sizeof a; i++) {
        pa[i] = (unsigned char)(i * 37 + (i >> 4));
        pb[i] = (unsigned char)(i * 91 + 3);
    }
    cpuid7(&ebx, &ecx);
    if (ebx >> 5 & 1) {
        two((__m256i *)acc, (__m256i *)a, (__m256i *)b, 8);
        h = hash(h, acc, sizeof acc);
    }
    if (ebx >> 30 & 1) {
        five(acc, a, b, 8);
        h = hash(h, acc, sizeof acc);
    }
    if (ecx >> 11 & 1) {
        dot(acc, a, b, 8);
        h = hash(h, acc, sizeof acc);
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
