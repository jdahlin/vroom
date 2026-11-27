/* 256- and 512-bit vectors of one runtime value in every element (broadcast
 * as 8, 16, 32 or 64 bits), and shifts by two constants in a row; results the
 * same as clang's.  Each half runs only on a CPU that has it. */
int printf(const char *, ...);
#ifdef __x86_64__
#include <immintrin.h>
#define N __attribute__((noinline, target("avx2,avx512f,avx512bw")))
N void s256(__m256i *o, const int *p, const short *w, const char *b, unsigned long v) {
    o[0] = _mm256_set1_epi32(p[0]); o[1] = _mm256_set1_epi16(w[1]); o[2] = _mm256_set1_epi8(b[2]);
    o[3] = _mm256_set1_epi32((int)v << 4 << 3); o[4] = _mm256_set1_epi64x(v >> 3 >> 5); o[5] = _mm256_set1_epi16(7);
}
N void s512(__m512i *o, const int *p, const short *w, const char *b) {
    o[0] = _mm512_set1_epi32(p[3]); o[1] = _mm512_set1_epi16(w[2]); o[2] = _mm512_set1_epi8(b[1]);
}
int main(void) {
    static __m512i o[8]; static int p[4] = {0x12345678, 2, 3, -7}; static short w[4] = {1, -2, 30000, 4}; static char b[4] = {5, -6, 100, 7};
    unsigned a, bb, c, d, h = 0;
    __asm__("cpuid" : "=a"(a), "=b"(bb), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    for (int i = 0; i < 20; i++) {
        p[0] += 0x1111111; p[3] -= 99; w[1] += 4000; b[2] += 77;
        if (bb >> 5 & 1) { s256((__m256i *)o, p, w, b, 0x9e3779b97f4a7c15ul * (i + 1)); for (int k = 0; k < 48; k++) h = h * 31 + ((unsigned *)o)[k]; }
        if (bb >> 30 & 1) { s512(o, p, w, b); for (int k = 0; k < 48; k++) h = h * 31 + ((unsigned *)o)[k]; }
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
