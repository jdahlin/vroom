// [ext:gnu_int128] __int128 and unsigned __int128: an extended integer type
// ([basic.fundamental]/2), its rank above long long's, mangled n and o
typedef unsigned __int128 u128;
static u128 g = 7;
static __int128 h = -3;
__int128 mul(__int128 a, __int128 b) { return a * b; }
unsigned __int128 shl(unsigned n) { return (unsigned __int128)1 << n; }
int main() {
    u128 x = shl(100);
    __int128 y = mul(-5, 1000000000000LL) * 1000000000000LL;
    if ((unsigned long long)(x >> 64) != (1ULL << 36)) return 1;
    if (y >= 0 || y / 1000000000000LL != -5000000000000LL) return 2;
    if (g + h != 4) return 3;
    unsigned __int128 m = -1;
    if ((unsigned long long)(m >> 100) != 0xfffffff) return 4;
    return 0;
}
