/* A constexpr variable as a builtin's immediate operand (Highway's
   _mm_shuffle_ps(v, v, kShuffle)) */
#if defined(__SSE2__)
#include <xmmintrin.h>

template <class T> __m128 shuffle(__m128 a)
{
    constexpr int k = sizeof(T) == 4 ? 0x1B : 0;
    return _mm_shuffle_ps(a, a, k);
}

int main()
{
    __m128 v = {1, 2, 3, 4};
    __m128 r = shuffle<int>(v);
    return r[0] != 4 || r[3] != 1;
}
#else
int main() { return 0; }
#endif
