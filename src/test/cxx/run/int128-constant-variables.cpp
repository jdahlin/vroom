/* cxx-run-flags: -std=c++17 */
// [expr.const]/4: constexpr variables and static data members of a 128-bit integer type whose
// values 64 bits do not hold (libc++'s numeric_limits<__int128>::__min and __max): usable in
// constant expressions, constant-initialized as data
typedef unsigned __int128 u128;
typedef __int128 i128;
constexpr const u128 big = ~u128(0) - 5;
template <class T> struct lim {
  static constexpr bool sg = T(-1) < T(0);
  static constexpr int digits = sizeof(T) * 8 - sg;
  static constexpr T mn = sg ? T(T(1) << digits) : 0;
  static constexpr T mx = sg ? T(T(~0) ^ mn) : T(~0);
  static constexpr int digits10 = static_cast<int>((digits * 1936274LL) / 6432163);
  static constexpr T max() { return mx; }
};
template <class T> struct traits {
  static constexpr int width = lim<T>::digits10 + 1;
};
static_assert(lim<i128>::mx > 0, "");
static_assert(lim<i128>::mn < 0, "");
static_assert(lim<i128>::mn + lim<i128>::mx == -1, "");
static_assert(lim<u128>::max() == big + 5, "");
static_assert((lim<u128>::mx >> 64) == ~0ULL, "");
static_assert(traits<u128>::width == 39, "");
int main() {
  u128 b = big;
  i128 m = lim<i128>::mn;
  const i128 &r = lim<i128>::mx;
  u128 top = lim<u128>::max();
  return b + 6 == 0 && m < 0 && -(m + 1) == r && top == ~u128(0) && traits<u128>::width == 39 ? 0 : 1;
}
