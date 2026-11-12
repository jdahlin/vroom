/* Branches on one bit, a mask or the sign (VIR at -O2): tbz/tbnz on bits 0, 7,
   31, 32 and 63, tst for a mask, cbz/cbnz and the sign bit for compares with 0
   and -1, and a tbz whose label is beyond its +-32 KB (relaxed to tbnz over b).
   Each function returns which way it went, so a wrong bit, width or polarity
   shows in main's checks. */
#define NI __attribute__((noinline))
typedef long long I;
typedef unsigned long long U;

static volatile int sink;

NI int b0(int x) { if (x & 1) return 10; return 20; }
NI int b7(int x) { if (x & 0x80) return 11; return 21; }
NI int b31(unsigned x) { if (x & 0x80000000u) return 12; return 22; }
NI int b32(U x) { if (x & 0x100000000ull) return 13; return 23; }
NI int b63(U x) { if (x & 0x8000000000000000ull) return 14; return 24; }
NI int bz(int x) { if (!(x & 0x10)) return 15; return 25; }
NI int beq(U x) { if ((x & 0x400) == 0) return 16; return 26; }
NI int bne(U x) { if ((x & 0x400000000000ull) != 0) return 17; return 27; }
NI int mask(int x) { if (x & 6) return 18; return 28; }
NI int maskz(U x) { if ((x & 0xff00000000ull) == 0) return 19; return 29; }
/* the and's value is also used: not folded away */
NI int both(int x) { int t = x & 0x80; if (t) return t + 1; return t + 2; }

NI int lt0(int x) { if (x < 0) return 30; return 40; }
NI int ge0(int x) { if (x >= 0) return 31; return 41; }
NI int lt0l(I x) { if (x < 0) return 32; return 42; }
NI int ge0l(I x) { if (x >= 0) return 33; return 43; }
NI int lem1(int x) { if (x <= -1) return 34; return 44; }
NI int gtm1(I x) { if (x > -1) return 35; return 45; }
NI int ule0(unsigned x) { if (x <= 0) return 36; return 46; }
NI int ugt0(U x) { if (x > 0) return 37; return 47; }
/* a byte loaded and widened: bit 7 of the byte, sign of the int */
NI int sbyte(const signed char *p) { if (*p < 0) return 38; return 48; }
NI int ubyte(const unsigned char *p) { if (*p & 0x80) return 39; return 49; }

/* a shift, then the mask (bit-fields): the bits of the shifted value */
struct BF { unsigned a : 6, b : 1, c : 3, d : 1; int s : 3; unsigned long long w : 40, top : 24; };
NI int bfb(const struct BF *p) { if (p->b) return 1; return 2; }
NI int bfc(const struct BF *p) { if (p->c) return 3; return 4; }
NI int bfs(const struct BF *p) { if (p->s < 0) return 5; return 6; }
NI int bftop(const struct BF *p) { if (p->top & 0x800000) return 7; return 8; }
NI int shr(U x) { if ((x >> 40) & 1) return 9; return 10; }
NI int sar(I x) { if ((x >> 60) & 0xf) return 11; return 12; }
/* the mask reaches past the shifted-in sign copies: not a test of x's bits */
NI int sar2(I x) { if ((x >> 62) & 0xc) return 13; return 14; }
NI int sar3(int x) { if ((x >> 30) & 4) return 15; return 16; }
/* a truncation between: only the low 32 bits of the masked value count */
NI int trn(U x) { if ((unsigned)((x >> 16) & 0xffffffffffull)) return 17; return 18; }

/* a loop that tests a bit of a value carried round it */
NI int bits(U x)
{
    int n = 0;

    for (int i = 0; i < 64; i++, x >>= 1)
        if (x & 1)
            n++;
    return n;
}

/* the branch over a body of more than 32 KB, both ways */
#define S1(k) sink = (k);
#define S8(k) S1(k) S1(k + 1) S1(k + 2) S1(k + 3) S1(k + 4) S1(k + 5) S1(k + 6) S1(k + 7)
#define S64(k) S8(k) S8(k + 8) S8(k + 16) S8(k + 24) S8(k + 32) S8(k + 40) S8(k + 48) S8(k + 56)
#define S512(k) S64(k) S64(k + 64) S64(k + 128) S64(k + 192) S64(k + 256) S64(k + 320) S64(k + 384) S64(k + 448)
NI int far(int x)
{
    if (x & 0x20) {
        S512(0) S512(512) S512(1024) S512(1536) S512(2048) S512(2560)
        return 50;
    }
    return 60;
}

NI int far2(int x)
{
    if (x & 0x40)
        return 70;
    S512(0) S512(512) S512(1024) S512(1536) S512(2048) S512(2560)
    return 80;
}

NI int farback(unsigned x)
{
    int n = 0;

    do {
        S512(0) S512(512) S512(1024) S512(1536) S512(2048) S512(2560)
        n++;
    } while ((x >>= 1) & 2);
    return n;
}

int main(void)
{
    signed char sc[2] = { -1, 1 };
    unsigned char uc[2] = { 0x80, 0x7f };

    if (b0(3) != 10 || b0(2) != 20 || b7(0x80) != 11 || b7(0x7f) != 21) return 1;
    if (b31(0x80000000u) != 12 || b31(0x7fffffff) != 22) return 2;
    if (b32(0x100000000ull) != 13 || b32(0xffffffffull) != 23 || b32(0x200000000ull) != 23) return 3;
    if (b63(1ull << 63) != 14 || b63(~0ull >> 1) != 24) return 4;
    if (bz(0) != 15 || bz(0x10) != 25 || beq(0x3ff) != 16 || beq(0x400) != 26) return 5;
    if (bne(0x400000000000ull) != 17 || bne(0x3fffffffffffull) != 27) return 6;
    if (mask(2) != 18 || mask(4) != 18 || mask(9) != 28 || maskz(0xffffffffull) != 19 || maskz(1ull << 32) != 29) return 7;
    if (both(0x80) != 0x81 || both(1) != 2) return 8;
    if (lt0(-1) != 30 || lt0(0) != 40 || ge0(0) != 31 || ge0(-5) != 41) return 9;
    if (lt0l(-1) != 32 || lt0l(0x7fffffff) != 42 || lt0l(0x80000000ll) != 42) return 10;
    if (ge0l(0x80000000ll) != 33 || ge0l(-0x80000000ll) != 43) return 11;
    if (lem1(-1) != 34 || lem1(0) != 44 || gtm1(-1) != 45 || gtm1(0) != 35) return 12;
    if (ule0(0) != 36 || ule0(1) != 46 || ugt0(1ull << 63) != 37 || ugt0(0) != 47) return 13;
    if (sbyte(&sc[0]) != 38 || sbyte(&sc[1]) != 48 || ubyte(&uc[0]) != 39 || ubyte(&uc[1]) != 49) return 14;
    {
        struct BF z = { 0 }, o = { 0 };

        o.b = 1, o.c = 4, o.s = -1, o.top = 0x800000;
        if (bfb(&z) != 2 || bfb(&o) != 1 || bfc(&z) != 4 || bfc(&o) != 3) return 19;
        if (bfs(&z) != 6 || bfs(&o) != 5 || bftop(&z) != 8 || bftop(&o) != 7) return 20;
    }
    if (shr(1ull << 40) != 9 || shr(1ull << 41) != 10 || sar(1ll << 63) != 11 || sar(1ll << 59) != 12) return 21;
    if (sar2(-1) != 13 || sar2(1ll << 62) != 14 || sar2(0x3fffffffffffffffll) != 14) return 22;
    if (sar3(-1) != 15 || sar3(0x40000000) != 16) return 23;
    if (trn(1ull << 16) != 17 || trn(1ull << 47) != 17 || trn(1ull << 48) != 18 || trn(0xffffull) != 18) return 24;
    if (bits(0x8000000000000001ull) != 2 || bits(~0ull) != 64) return 15;
    if (far(0x20) != 50 || sink != 3071 || far(0x1f) != 60) return 16;
    if (far2(0x40) != 70 || far2(0x3f) != 80) return 17;
    if (farback(14) != 3 || farback(0) != 1 || farback(5) != 2) return 18;
    return 0;
}
