/* Exact 64-bit arithmetic stored through a 128-bit overflow result pointer. */
#if defined(__SIZEOF_INT128__)
typedef __int128 I;
typedef unsigned __int128 U;
static volatile unsigned long long umax = ~0ULL, uzero;
static volatile long long smin = (-9223372036854775807LL - 1), sminus = -1, stwo = -2;
#endif
int main(void)
{
#if defined(__SIZEOF_INT128__)
    I i;
    U u;
    if (__builtin_mul_overflow(umax, umax, &u) || u != (U)~0ULL * (U)~0ULL) return 1;
    if (!__builtin_mul_overflow(umax, umax, &i) || (U)i != u) return 2;
    if (__builtin_add_overflow(umax, umax, &i) || i != ((I)1 << 65) - 2) return 3;
    if (!__builtin_sub_overflow(uzero, umax, &u) || u != (U)0 - (U)~0ULL) return 4;
    if (__builtin_sub_overflow(uzero, umax, &i) || i != -(I)~0ULL) return 5;
    if (!__builtin_add_overflow(sminus, uzero, &u) || u != (U)-1) return 6;
    if (__builtin_mul_overflow(umax, sminus, &i) || i != -(I)~0ULL) return 7;
    if (!__builtin_mul_overflow(umax, stwo, &u) || u != (U)0 - ((U)~0ULL * 2)) return 8;
    if (__builtin_mul_overflow(smin, smin, &i) || i != ((I)1 << 126)) return 9;
    if (__builtin_mul_overflow(smin, stwo, &u) || u != ((U)1 << 64)) return 10;
#endif
    return 0;
}
