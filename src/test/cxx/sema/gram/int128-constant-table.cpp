// a constexpr array of 128-bit integers whose elements 64 bits do not hold (libc++'s __pow10_128 in <__charconv/tables.h>): the initialization is a constant expression, [expr.const]; every value is clang++'s
typedef unsigned long long u64;
typedef unsigned __int128 u128;
inline constexpr const u128 pow10_128[6] = {
    u64(0),
    u64(10),
    u64(10000000000000000000ull),
    u128(u64(10000000000000000000ull)) * u64(10),
    u128(u64(10000000000000000000ull)) * u64(10000000000000000000ull),
    (u128(u64(10000000000000000000ull)) * u64(10000000000000000000ull)) * 10};
static_assert(u128(u64(10000000000000000000ull)) * u64(10) / 10 == u64(10000000000000000000ull));
static_assert(u128(u64(10000000000000000000ull)) * u64(10) > u128(~0ull));
