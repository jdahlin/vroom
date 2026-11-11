/* vcc-flags: -std=c23 */
#define L(x,n) __builtin_stdc_rotate_left(x,n)
#define R(x,n) __builtin_stdc_rotate_right(x,n)
_Static_assert(L((unsigned char)129, 1) == 3, "narrow left");
_Static_assert(R((unsigned char)129, 1) == 192, "narrow right");
_Static_assert(L(1u, 0x100000001ULL) == 2, "wide count");
_Static_assert(L(1u, 32) == 1 && R(1u, 0) == 1, "zero modulo width");
_Static_assert(__builtin_types_compatible_p(__typeof__(L((unsigned short)1,1)),unsigned short), "type retained");
static unsigned xv=0x80000001u; static unsigned calls;
static unsigned value(void) { calls++; return xv; }
static unsigned shift(void) { calls++; return 1; }
int main(void) {
    unsigned a = L(value(), shift());
    if (a != 3 || calls != 2) return 1;
    unsigned char x = 0x81;
    for (unsigned i=0;i<100;i++) {
        unsigned char y = L(x, i);
        if (R(y, i) != x) return 2;
    }
    unsigned long long big = 0x8000000000000001ULL;
    if (L(big, 1) != 3 || R(big, 1) != 0xc000000000000000ULL) return 3;
    volatile unsigned _BitInt(7) narrow = 65;
    if (L(narrow, 1) != 3 || R(narrow, 1) != 96) return 4;
    volatile unsigned __int128 wide = ((unsigned __int128)1 << 127) | 1;
    if (L(wide, 1) != 3 || R(wide, 1) != ((unsigned __int128)3 << 126)) return 5;
    if (L((_Bool)1, 9) != 1) return 6;
    volatile unsigned _BitInt(1) one = 1;
    if (L(1u, one) != 2) return 7;
    unsigned __int128 count = ((unsigned __int128)1 << 100) + 1;
    if (L(1u, count) != 2) return 8;
    return 0;
}
