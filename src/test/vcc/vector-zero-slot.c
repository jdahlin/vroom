/* A 16-byte object that only a memset writes is a zero vector, in a function
 * with parameters (which VIR must keep first); x86-64 also widens 8 loaded
 * bytes through a zero upper lane. */
typedef int v4 __attribute__((vector_size(16)));
typedef long long v2 __attribute__((vector_size(16)));
int printf(const char *, ...);
__attribute__((noinline)) static v4 greater(const v4 *a, int n)
{
    v4 zero, sum;
    __builtin_memset(&zero, 0, sizeof zero);
    __builtin_memset(&sum, 0, sizeof sum);
    for (int i = 0; i < n; i++)
        sum += a[i] > zero;
    return sum;
}
#ifdef __x86_64__
#include <emmintrin.h>
__attribute__((noinline)) static __m128i *widen(__m128i *o, const void *p)
{
    __m128i half = _mm_loadl_epi64((const __m128i *)p);
    *o = _mm_unpacklo_epi8(half, _mm_setzero_si128());
    return o;
}
#endif
int main(void)
{
    v4 in[3] = {{1, -1, 0, 5}, {-3, 4, 7, 0}, {9, 9, -9, 2}};
    v4 r = greater(in, 3);
    printf("%d %d %d %d\n", r[0], r[1], r[2], r[3]);
#ifdef __x86_64__
    unsigned char bytes[16] = {1, 200, 3, 255, 5, 6, 7, 128, 99, 99, 99};
    __m128i out;
    const short *s = (const short *)widen(&out, bytes);
    for (int i = 0; i < 8; i++)
        printf("%d ", s[i]);
    printf("\n");
#endif
    return 0;
}
