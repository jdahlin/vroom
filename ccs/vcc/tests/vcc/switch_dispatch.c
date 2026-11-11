/* Balanced switch dispatch: mathematical oracles independent of case order.
 * No libc/target instructions, also exercised at O0 and in wasm32/64. */
#define NI __attribute__((noinline))
static volatile unsigned evaluations;
static NI int select_once(int x) { evaluations++; return x; }
NI int sw_sparse32(int x)
{
    switch (x) {
    default: return -7;
    case -2055: return 1;
    case -548: return 12;
    case 959: return 23;
    case -1781: return 3;
    case -274: return 14;
    case 1233: return 25;
    case -1507: return 5;
    case 0: return 16;
    case 1507: return 27;
    case -1233: return 7;
    case 274: return 18;
    case 1781: return 29;
    case -959: return 9;
    case 548: return 20;
    case 2055: return 31;
    case -685: return 11;
    case 822: return 22;
    case -1918: return 2;
    case -411: return 13;
    case 1096: return 24;
    case -1644: return 4;
    case -137: return 15;
    case 1370: return 26;
    case -1370: return 6;
    case 137: return 17;
    case 1644: return 28;
    case -1096: return 8;
    case 411: return 19;
    case 1918: return 30;
    case -822: return 10;
    case 685: return 21;
    }
}
NI int sw_ranges32(int x)
{
    switch (x) {
    case -2055 ... -2052: return 1;
    case -548 ... -545: return 12;
    case 959 ... 962: return 23;
    case -1781 ... -1778: return 3;
    case -274 ... -271: return 14;
    case 1233 ... 1236: return 25;
    case -1507 ... -1504: return 5;
    case 0 ... 3: return 16;
    case 1507 ... 1510: return 27;
    case -1233 ... -1230: return 7;
    case 274 ... 277: return 18;
    case 1781 ... 1784: return 29;
    case -959 ... -956: return 9;
    case 548 ... 551: return 20;
    case 2055 ... 2058: return 31;
    case -685 ... -682: return 11;
    case 822 ... 825: return 22;
    case -1918 ... -1915: return 2;
    case -411 ... -408: return 13;
    case 1096 ... 1099: return 24;
    case -1644 ... -1641: return 4;
    case -137 ... -134: return 15;
    case 1370 ... 1373: return 26;
    case -1370 ... -1367: return 6;
    case 137 ... 140: return 17;
    case 1644 ... 1647: return 28;
    case -1096 ... -1093: return 8;
    case 411 ... 414: return 19;
    case 1918 ... 1921: return 30;
    case -822 ... -819: return 10;
    case 685 ... 688: return 21;
    default: return -9;
    }
}
NI int sw_unsigned64(unsigned long long x)
{
    switch (x) {
    case 4294967296ULL: return 18;
    case 1ULL: return 12;
    case 93ULL: return 21;
    case -1: return 15;
    case 9223372036854775807ULL: return 13;
    case 9223372036854775552ULL: return 20;
    case 18446744073709551614ULL: return 16;
    case 1234567890123456ULL: return 22;
    case 0ULL: return 11;
    case 9223372036854775824ULL: return 19;
    case 9223372036854775808ULL: return 14;
    case 18446744069414584320ULL: return 17;
    default: return -11;
    }
}
NI int sw_unsigned32(unsigned x)
{
    switch (x) {
    case -1: return 1;
    case 0: return 2;
    case 1: return 3;
    case 2147483647U: return 4;
    case 2147483648U: return 5;
    case 4294967293U ... 4294967294U: return 6;
    case 91: return 7;
    case 9100: return 8;
    case 910000: return 9;
    default: return -17;
    }
}
NI int sw_signed64(long long x)
{
    switch (x) {
    case (-9223372036854775807LL-1): return 1;
    case (-9223372036854775807LL-1)+1 ... (-9223372036854775807LL-1)+3: return 2;
    case -5000000000LL: return 3;
    case -900: return 4;
    case -1: return 5;
    case 0: return 6;
    case 1: return 7;
    case 900: return 8;
    case 5000000000LL: return 9;
    case 9223372036854775806LL ... 9223372036854775807LL: return 10;
    default: return -13;
    }
}
NI int sw_broad64(unsigned long long x)
{
    switch (x) {
    case 8ULL ... 18446744073709551607ULL: return 42;
    case 0: return 1; case 1: return 2; case 2: return 3;
    case 3: return 4; case 4: return 5; case 5: return 6;
    case 18446744073709551614ULL: return 7;
    case 18446744073709551615ULL: return 8;
    default: return -1;
    }
}
NI int sw_cross32(int x)
{
    switch (x) {
    case (-2147483647-1)+4 ... 2147483647-4: return 42;
    case (-2147483647-1): return 1; case (-2147483647-1)+1: return 2;
    case (-2147483647-1)+2: return 3; case (-2147483647-1)+3: return 4;
    case 2147483647-3: return 5; case 2147483647-2: return 6;
    case 2147483647-1: return 7; case 2147483647: return 8;
    default: return -1;
    }
}
NI int sw_dense(int x)
{
    switch (x) {
    case -3: return 12;
    case -2: return 9;
    case -1: return 72;
    case 0: return 5;
    case 1: return 33;
    case 2: return 41;
    case 3: return 99;
    case 4: return 17;
    default: return -1;
    }
}
static NI int control(int x, int enter)
{
    int s = 0;
    if (enter) goto bypass;
    switch (select_once(x)) {
    case -1000: s = 5; /* fallthrough */
    case -100: s += 2; break;
    default: s = 19; break;
    case -10: if (0) { case 7: s = 37; break; } s = 3; break;
    case 11: s = 11; break;
    case 100: s = 100; break;
    case 1000: s = 1000; break;
    case 10000: s = 10000; break;
    case 100000: s = 100000; break;
    bypass: s = 23; break;
    }
    return s;
}
static NI int missing(int x)
{
    int r = 7;
    switch (x) {
    case 1: r = 1; break; case 10: r = 2; break;
    case 100: r = 3; break; case 1000: r = 4; break;
    case 10000: r = 5; break; case 100000: r = 6; break;
    case 1000000: r = 8; break; case 10000000: r = 9; break;
    }
    return r;
}
static NI int wide(__int128 x)
{
    switch (x) {
    case -((__int128)1<<100): return 1;
    case -9000: return 2; case -90: return 3; case -9: return 4;
    case 0: return 5; case 9: return 6; case 90: return 7;
    case 9000: return 8; case ((__int128)1<<100): return 9;
    default: return -1;
    }
}
static const __int128 huge = (__int128)1 << 100;
int main(void)
{
    for (int x = -2300; x <= 2300; x++) {
        int d = x + 2055;
        int expected = d >= 0 && d / 137 < 31 && d % 137 == 0 ? d / 137 + 1 : -7;
        if (sw_sparse32(x) != expected) return 1;
        expected = d >= 0 && d / 137 < 31 && d % 137 <= 3 ? d / 137 + 1 : -9;
        if (sw_ranges32(x) != expected) return 2;
    }
    const unsigned long long keys[] = {0ULL, 1ULL, 9223372036854775807ULL, 9223372036854775808ULL, 18446744073709551615ULL, 18446744073709551614ULL, 18446744069414584320ULL, 4294967296ULL, 9223372036854775824ULL, 9223372036854775552ULL, 93ULL, 1234567890123456ULL};
    for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++) {
        if (sw_unsigned64(keys[i]) != (int)i + 11) return 3;
        for (int delta = -2; delta <= 2; delta++) {
            unsigned long long x = keys[i] + (unsigned long long)delta;
            int expected = -11;
            for (unsigned j = 0; j < sizeof keys / sizeof *keys; j++)
                if (x == keys[j]) expected = (int)j + 11;
            if (sw_unsigned64(x) != expected) return 4;
        }
    }
    const unsigned keys32[] = {4294967295U, 0, 1, 2147483647U, 2147483648U,
        4294967293U, 91, 9100, 910000};
    for (unsigned i = 0; i < 9; i++)
        if (sw_unsigned32(keys32[i]) != (int)i + 1) return 12;
    if (sw_unsigned32(4294967294U) != 6 || sw_unsigned32(2147483649U) != -17)
        return 13;
    const long long signedkeys[] = {(-9223372036854775807LL-1),
        (-9223372036854775807LL-1)+1, -5000000000LL, -900, -1, 0, 1,
        900, 5000000000LL, 9223372036854775807LL};
    for (unsigned i = 0; i < 10; i++)
        if (sw_signed64(signedkeys[i]) != (int)i + 1) return 5;
    if (sw_signed64((-9223372036854775807LL-1)+3) != 2 ||
        sw_signed64(9223372036854775806LL) != 10 || sw_signed64(777) != -13) return 6;
    const unsigned long long broad[] = {0, 5, 6, 7, 8, 9, 0x8000000000000000ULL,
        18446744073709551607ULL, 18446744073709551608ULL,
        18446744073709551614ULL, 18446744073709551615ULL};
    const int broad_expected[] = {1, 6, -1, -1, 42, 42, 42, 42, -1, 7, 8};
    for (unsigned i = 0; i < sizeof broad / sizeof *broad; i++)
        if (sw_broad64(broad[i]) != broad_expected[i]) return 14;
    const int cross[] = {(-2147483647-1), (-2147483647-1)+3, (-2147483647-1)+4,
        -1, 0, 1, 2147483647-4, 2147483647-3, 2147483647};
    const int cross_expected[] = {1, 4, 42, 42, 42, 42, 42, 5, 8};
    for (unsigned i = 0; i < sizeof cross / sizeof *cross; i++)
        if (sw_cross32(cross[i]) != cross_expected[i]) return 15;
    const int dense[] = {12, 9, 72, 5, 33, 41, 99, 17};
    for (int i = -4; i <= 5; i++)
        if (sw_dense(i) != (i >= -3 && i <= 4 ? dense[i+3] : -1)) return 7;
    if (control(-1000, 0) != 7 || control(-100, 0) != 2 ||
        control(7, 0) != 37 || control(-10, 0) != 3 || control(88, 0) != 19 ||
        control(100000, 0) != 100000 || evaluations != 6) return 8;
    if (control(0, 1) != 23 || evaluations != 6) return 9;
    if (missing(5) != 7 || missing(10000000) != 9 || missing(1) != 1) return 10;
    if (wide(-huge) != 1 || wide(huge) != 9 ||
        wide(9) != 6 || wide(10) != -1) return 11;
    return 0;
}
