/* cxx-run-flags: -std=c++17 */
// [expr.shift]/2 before C++20 (CWG1457): a non-negative signed E1 << E2 is E1 x 2^E2 when that is
// representable in the unsigned type (libc++'s numeric_limits<__int128>::__min: __int128(1) << 127)
typedef __int128 i128;
constexpr i128 a = i128(3) << 60;
static_assert(a == i128(3) * 1152921504606846976LL, "");
static_assert((i128(5) << 0) == 5, "");
static_assert(((i128(1) << 100) >> 98) == 4, "");
static_assert(((i128(1) << 127) != 0) == true, "");
template <int N> struct bits { static constexpr int v = N; };
int main() { return a > 0 && bits<((i128(7) << 120) >> 120) == 7>::v == 1 ? 0 : 1; }
