/* Membership dispatch must preserve bodies, fallthrough and one selector read. */
#define NI __attribute__((noinline))
typedef unsigned long long U;
#define MIN64 (-9223372036854775807LL - 1)
static volatile unsigned reads, effects;
static NI int next(int x) { reads++; return x; }
NI int bs32(int x)
{
    switch (x) { case 0: case 3: case 7: case 11: case 19: case 31: return 17; default: return 5; }
}
NI int bs64(U x)
{
    switch (x) { case 1: case 5: case 9: case 17: case 33: case 42: case 60: case 63: return 17; default: return 5; }
}
NI int bs_negative(int x)
{
    switch (x) { case -64: case -60: case -57: case -49: case -31: case -22: case -4: case -1: return 17; default: return 5; }
}
NI int bs_min(long long x)
{
    switch (x) { case MIN64: case MIN64+2: case MIN64+6: case MIN64+18:
    case MIN64+33: case MIN64+49: case MIN64+58: case MIN64+63: return 17; default: return 5; }
}
NI int bs_umax(U x)
{
    switch (x) { case 0xffffffffffffffc0ULL: case 0xffffffffffffffc2ULL:
    case 0xffffffffffffffc6ULL: case 0xffffffffffffffd2ULL: case 0xffffffffffffffe1ULL:
    case 0xfffffffffffffff1ULL: case 0xfffffffffffffffaULL: case 0xffffffffffffffffULL:
    return 17; default: return 5; }
}
NI int bs_u32(unsigned x)
{
    switch (x) { case 0xffffffc0U: case 0xffffffc2U: case 0xffffffc6U: case 0xffffffd2U:
    case 0xffffffe1U: case 0xfffffff1U: case 0xfffffffaU: case 0xffffffffU: return 17; default: return 5; }
}
NI int bs_ranges(int x)
{
    switch (x) { case 0 ... 2: case 7 ... 9: case 17 ... 18: case 31: return 17; default: return 5; }
}
NI int bs_reverse(int x)
{
    switch (x) { case 63: case 33: case 17: case 0: return 17; default: return 5; }
}
NI int bs_first(int x)
{
    switch (x) { default: return 5; case 0: case 3: case 7: case 11: case 19: case 31: return 17; }
}
NI int bs_no_default(int x)
{
    int r = 5;
    switch (x) { case 0: case 3: case 7: case 11: case 19: case 31: r = 17; break; }
    return r;
}
NI int bs_selector(int x)
{
    switch (next(x)) { case 0: case 3: case 7: case 11: case 19: case 31: return 17; default: return 5; }
}
NI int bs_fallthrough(int x)
{
    unsigned before = effects;
    switch (x) {
    case 0: case 3: case 7: case 11: case 19: case 31: effects += 10;
    default: effects++;
    }
    return (int)(effects - before);
}
/* Conservative negative controls: distinct source entries, full range, span64,
 * and default sharing the very same destination as the case run. */
NI int bs_few(int x)
{
    switch (x) { case 0: case 3: case 31: return 17; default: return 5; }
}
NI int bs_nonstacked(int x)
{
    switch (x) { case 0: goto out; case 3: goto out; case 7: goto out; case 11: goto out; default: return 5; }
out: return 17;
}
NI int bs_dense(int x)
{
    switch (x) { case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: return 17; default: return 5; }
}
NI int bs_wide(int x)
{
    switch (x) { case 0: case 3: case 7: case 64: return 17; default: return 5; }
}
NI int bs_shared_default(int x)
{
    switch (x) { case 0: case 3: default: case 7: case 11: return 17; }
}
static int in32(int x) { return x==0 || x==3 || x==7 || x==11 || x==19 || x==31; }
static int in64(U x) { return x==1 || x==5 || x==9 || x==17 || x==33 || x==42 || x==60 || x==63; }
static int negative(int x) { return x==-64 || x==-60 || x==-57 || x==-49 || x==-31 || x==-22 || x==-4 || x==-1; }
static int shifted(U x) { return x==0 || x==2 || x==6 || x==18 || x==33 || x==49 || x==58 || x==63; }
int main(void)
{
    for (int x = -130; x <= 130; x++) {
        int want = in32(x) ? 17 : 5;
        if (bs32(x)!=want || bs_first(x)!=want || bs_no_default(x)!=want) return 1;
        unsigned before = reads;
        if (bs_selector(x)!=want || reads!=before+1) return 2;
        if (bs_fallthrough(x)!=(in32(x)?11:1)) return 3;
        if (bs64((U)x)!=(in64((U)x)?17:5)) return 4;
        if (bs_negative(x)!=(negative(x)?17:5)) return 5;
        int range = (x>=0&&x<=2)||(x>=7&&x<=9)||(x>=17&&x<=18)||x==31;
        if (bs_ranges(x)!=(range?17:5)) return 6;
        if (bs_nonstacked(x)!=((x==0||x==3||x==7||x==11)?17:5)) return 7;
        if (bs_dense(x)!=((x>=0&&x<=7)?17:5) || bs_shared_default(x)!=17) return 8;
        if (bs_wide(x)!=((x==0||x==3||x==7||x==64)?17:5)) return 9;
        if (bs_reverse(x)!=((x==0||x==17||x==33||x==63)?17:5)) return 14;
        if (bs_few(x)!=((x==0||x==3||x==31)?17:5)) return 15;
    }
    for (U i=0; i<131; i++) {
        U u = 0xffffffffffffffc0ULL + i;
        unsigned v = 0xffffffc0U + (unsigned)i;
        if (bs_umax(u)!=(shifted(i)?17:5) || bs_u32(v)!=(shifted(i)?17:5)) return 10;
        if (bs_min(MIN64+(long long)i)!=(shifted(i)?17:5)) return 11;
    }
    if (bs_min(0)!=5 || bs_min(9223372036854775807LL)!=5 || bs_umax(0x10000003fULL)!=5) return 12;
    if (bs64(0x100000000ULL)!=5 || bs64(0xffffffffffffffffULL)!=5) return 13;
    return 0;
}
