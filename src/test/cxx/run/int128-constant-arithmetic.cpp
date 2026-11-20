/* [expr.const], [conv.integral], [expr.mul], [expr.add], [expr.shift]: a
   constant expression of a 128-bit integer type, whose value 64 bits do
   not hold, in static_asserts; the arrays hold what 64 bits do.  The
   wide table is sema/gram/int128-constant-table.cpp. */
typedef unsigned long long u64;
typedef unsigned __int128 u128;
typedef __int128 i128;

static_assert(u128(u64(10000000000000000000ull)) * 10 > u128(~0ull));
static_assert(u128(u64(10000000000000000000ull)) * 10 / 10 ==
              u64(10000000000000000000ull));
static_assert((u128(1) << 127) >> 127 == 1);
static_assert((u128(1) << 64) - 1 == u128(~0ull));
static_assert(u128(0) - 1 > u128(1) << 126);
static_assert(i128(-7) / 2 == -3 && i128(-7) % 2 == -1);
static_assert((i128(-1) >> 3) == -1);
static_assert(i128(u128(1) << 127) < 0);
static_assert(u64(u128(1) << 64 | 5) == 5);
static_assert(!u128(0) && !!(u128(1) << 100));
static_assert((u128(0xff) & 0x0f) == 0x0f && (u128(1) ^ 3) == 2);
static_assert(+u128(3) == 3);

int main()
{
    constexpr u128 table[3] = {u64(0), u64(10), u128(u64(1000)) * u64(10)};
    constexpr i128 negative[2] = {-5, i128(1) << 40};
    if (static_cast<u64>(table[2]) != 10000)
        return 1;
    if (static_cast<u64>(negative[0]) != static_cast<u64>(-5))
        return 2;
    if (static_cast<u64>(negative[1] >> 30) != 1024)
        return 3;
    return 0;
}
