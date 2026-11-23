/* [ext:gnu_library_builtins] the integer builtins clang evaluates in a
   constant expression: __builtin_clz/ctz/popcount/ffs/parity/clrsb of
   int, long and long long, __builtin_bswap16/32/64, __builtin_bitreverse*,
   __builtin_rotateleft*, __builtin_expect, __builtin_abs (GCC manual
   6.56). */
static_assert(__builtin_clz(1u) == 31);
static_assert(__builtin_clzl(1ul) == 63);
static_assert(__builtin_clzll(0x10ull) == 59);
static_assert(__builtin_ctz(8u) == 3);
static_assert(__builtin_ctzl(1ul << 40) == 40);
static_assert(__builtin_ctzll(6ull) == 1);
static_assert(__builtin_popcount(0xffu) == 8);
static_assert(__builtin_popcountl(7ul) == 3);
static_assert(__builtin_popcountl(1ULL << 40) == (sizeof(long) == 8));
static_assert(__builtin_popcountll(~0ull) == 64);
static_assert(__builtin_ffs(0) == 0);
static_assert(__builtin_ffs(12) == 3);
static_assert(__builtin_ffsl(1l << 20) == 21);
static_assert(__builtin_parity(7u) == 1);
static_assert(__builtin_parityll(3ull) == 0);
static_assert(__builtin_clrsb(0) == 31);
static_assert(__builtin_clrsb(-1) == 31);
static_assert(__builtin_clrsb(1) == 30);
static_assert(__builtin_bswap16(0x1234) == 0x3412);
static_assert(__builtin_bswap32(0x12345678u) == 0x78563412u);
static_assert(__builtin_bswap64(0x0102030405060708ull) ==
              0x0807060504030201ull);
static_assert(__builtin_bitreverse8(1) == 0x80);
static_assert(__builtin_bitreverse16(1) == 0x8000);
static_assert(__builtin_bitreverse32(1u) == 0x80000000u);
static_assert(__builtin_bitreverse64(1ull) == 0x8000000000000000ull);
static_assert(__builtin_rotateleft8(0x81, 1) == 0x03);
static_assert(__builtin_rotateright16(1, 1) == 0x8000);
static_assert(__builtin_rotateleft32(0x80000001u, 4) == 0x18u);
static_assert(__builtin_rotateright64(1ull, 4) == 0x1000000000000000ull);
static_assert(__builtin_expect(5, 1) == 5);
static_assert(__builtin_abs(-3) == 3);
static_assert(__builtin_labs(-3l) == 3);
static_assert(__builtin_llabs(-3ll) == 3);

constexpr int count_bits(unsigned long long v)
{
    int n = 0;
    while (v) {
        n += __builtin_ctzll(v) >= 0;
        v &= v - 1;
    }
    return n;
}
static_assert(count_bits(0xf0f0) == __builtin_popcountll(0xf0f0));

int main()
{
    volatile unsigned x = 0x00f0;
    if (__builtin_clz(x) != 24 || __builtin_ctz(x) != 4)
        return 1;
    if (__builtin_popcount(x) != 4 || __builtin_ffs(x) != 5)
        return 2;
    if (__builtin_bswap32(x) != 0xf0000000u)
        return 3;
    if (__builtin_bitreverse32(x) != 0x0f000000u)
        return 4;
    if (__builtin_rotateleft32(x, 28) != 0x0000000fu)
        return 5;
    return 0;
}
