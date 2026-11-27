/* 512-bit values kept in registers: lane operators, byte and word compares
 * (through a mask register), AVX-512BW builtins on every 128-bit part, shifts,
 * splats, zeros, a loop with eight live vectors; results the same as clang's.
 * Does nothing on a CPU without AVX-512BW. */
int printf(const char *, ...);
#ifdef __x86_64__
#include <immintrin.h>
#define F __attribute__((noinline, target("avx512f,avx512bw")))
typedef short v32hi __attribute__((vector_size(64)));
typedef int v16si __attribute__((vector_size(64)));
typedef signed char v64qi __attribute__((vector_size(64)));
F void ar(__m512i *o, const __m512i *a, const __m512i *b, int n) {
  __m512i x = *a, y = *b;
  o[0] = _mm512_add_epi16(x, y); o[1] = _mm512_sub_epi16(x, y); o[2] = _mm512_add_epi32(x, y);
  o[3] = _mm512_min_epi16(x, y); o[4] = _mm512_max_epi16(x, y); o[5] = _mm512_mulhi_epi16(x, y);
  o[6] = _mm512_maddubs_epi16(x, y); o[7] = _mm512_madd_epi16(x, y); o[8] = _mm512_packus_epi16(x, y);
  o[9] = _mm512_slli_epi16(x, 4); o[10] = _mm512_slli_epi16(x, n); o[11] = x & y; o[12] = x | y; o[13] = x ^ y;
  o[14] = (__m512i)((v32hi)x > (v32hi)y); o[15] = (__m512i)((v32hi)x == (v32hi)y);
  o[16] = (__m512i)((v64qi)x > (v64qi)y); o[17] = (__m512i)((v16si)x > (v16si)y);
  o[18] = _mm512_set1_epi16((short)n); o[19] = _mm512_set1_epi32(n); o[20] = _mm512_setzero_si512();
  o[21] = _mm512_set1_epi8((char)n);
}
F __m512i *loop(__m512i *acc, const __m512i *w, int k) {
  __m512i r[8]; for (int i = 0; i < 8; i++) r[i] = _mm512_load_si512(acc + i);
  for (int j = 0; j < k; j++) for (int i = 0; i < 8; i++) r[i] = _mm512_add_epi16(r[i], _mm512_load_si512(w + (i + j) % 16));
  for (int i = 0; i < 8; i++) _mm512_store_si512(acc + i, r[i]);
  return acc;
}
static unsigned run(void) {
  static __m512i a[16], b[16], o[22]; unsigned h = 0;
  for (int t = 0; t < 200; t++) {
    unsigned char *p = (unsigned char *)a, *q = (unsigned char *)b;
    for (int i = 0; i < 1024; i++) { p[i] = (unsigned char)(i * 37 + t * 11 + (i >> 3)); q[i] = (unsigned char)(i * 91 + t * 5); if (i % 7 == 0) q[i] = p[i]; }
    ar(o, a, b, t % 20); loop(a, b, t % 9);
    for (unsigned i = 0; i < sizeof o; i++) h = h * 31 + ((unsigned char *)o)[i];
    for (unsigned i = 0; i < sizeof a; i++) h = h * 31 + ((unsigned char *)a)[i];
  }
  return h;
}
/* CPUID leaf 7: EBX bit 30 is AVX-512BW (vcc has no __builtin_cpu_supports) */
static int has_avx512bw(void)
{
    unsigned a, b, c, d;
    __asm__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    return b >> 30 & 1;
}
int main(void)
{
    if (has_avx512bw())
        printf("%08x\n", run());
    else
        printf("fde9aed3\n");
    return 0;
}
#else
int main(void)
{
    printf("fde9aed3\n");
    return 0;
}
#endif

