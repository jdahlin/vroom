#if defined(__x86_64__)
#include <immintrin.h>
typedef unsigned uv4 __attribute__((vector_size(16)));
static int calls;
static __m128i arg(__m128i x) { calls++; return x; }
__attribute__((target("avx"))) int main(void)
{
    uv4 x = {0x80000000u,0xffffffffu,0x7fffffffu,0xffff0001u};
    uv4 y = {3,0xdeadbeefu,0x10001u,0x80000000u};
    uv4 absx = (uv4)_mm_abs_epi32(arg((__m128i)x));
    if (calls != 1 || absx[0] != 0x80000000u || absx[1] != 1 || absx[2] != 0x7fffffffu || absx[3] != 0xffffu) return 1;
    uv4 product = (uv4)_mm_mullo_epi32(arg((__m128i)absx),arg((__m128i)y));
    if (calls != 3) return 2;
    for (int i=0;i<4;i++) if (product[i] != (unsigned)((unsigned long long)absx[i]*y[i])) return 3;
    return 0;
}
#else
int main(void) { return 0; }
#endif
