// [basic.start.static]/2: a constant-initialized array of a 128-bit integer type whose elements'
// values do not fit 64 bits (libc++'s __pow10_128: __uint128_t(10000000000000000000) * 10)
typedef unsigned __int128 u128;
inline constexpr const u128 pow10[4] = {
    1, u128(10000000000000000000ULL), u128(10000000000000000000ULL) * 10ULL,
    (u128(10000000000000000000ULL) * 10000000000000000000ULL) * 10};
int main() {
  u128 p = 10000000000000000000ULL;
  return pow10[0] == 1 && pow10[2] == p * 10 && pow10[3] == p * p * 10 && pow10[3] / pow10[2] == p
             ? 0
             : 1;
}
