/* x87 fistp is signed: unsigned 64-bit conversions need both halves. */
static volatile long double input[] = {
    -0.75L, 0.0L, 1.75L, 4294967295.75L,
    9223372036854775807.0L, 9223372036854775808.0L,
    9223372036854775809.0L, 18446744073709551615.0L
};
static const unsigned long long expect[] = {
    0, 0, 1, 4294967295ULL, 9223372036854775807ULL,
    9223372036854775808ULL, 9223372036854775809ULL, 18446744073709551615ULL
};
static volatile unsigned long long addend = 7;
static int calls;
static long double next(int i) { calls++; return input[i]; }
int main(void)
{
    for (int i = 0; i < 8; i++) {
        unsigned long long got = addend + (unsigned long long)next(i);
        if (got != expect[i] + 7 || calls != i + 1) return i + 1;
    }
#if defined(__x86_64__)
    unsigned short before, after, down;
    __asm__ volatile("fnstcw %0" : "=m"(before));
    down = (before & ~0xc00u) | 0x400u;
    __asm__ volatile("fldcw %0" : : "m"(down));
    unsigned long long got = (unsigned long long)next(6);
    __asm__ volatile("fnstcw %0" : "=m"(after));
    __asm__ volatile("fldcw %0" : : "m"(before));
    if (got != expect[6] || after != down || calls != 9) return 9;
#endif
    return 0;
}
