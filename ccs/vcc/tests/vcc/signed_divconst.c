/* Constant signed division/remainder: truncation toward zero, extremes and effects. */
#define NI __attribute__((noinline))
#define IMAX 2147483647
#define IMIN (-IMAX - 1)
#define LMAX 9223372036854775807LL
#define LMIN (-LMAX - 1)
#define DIV32(name, divisor) \
    NI int sd32_q_##name(int x) { return x / (divisor); } \
    NI int sd32_r_##name(int x) { return x % (divisor); }
#define DIV64(name, divisor) \
    NI long long sd64_q_##name(long long x) { return x / (divisor); } \
    NI long long sd64_r_##name(long long x) { return x % (divisor); }
DIV32(p3, 3) DIV32(n3, -3)
/* Equality in the reciprocal bound, including nonzero short shifts. */
DIV32(p6, 6) DIV32(n6, -6)
DIV32(p12, 12) DIV32(n12, -12)
DIV32(p7, 7) DIV32(n7, -7)
DIV32(p10, 10) DIV32(n10, -10)
DIV32(p37, 37) DIV32(n37, -37)
DIV32(plarge, 1073741825) DIV32(nlarge, -1073741825)
DIV32(pmax, IMAX) DIV32(nmax, -IMAX)
DIV64(p3, 3LL) DIV64(n3, -3LL)
DIV64(p6, 6LL) DIV64(n6, -6LL)
DIV64(p12, 12LL) DIV64(n12, -12LL)
DIV64(p9, 9LL) DIV64(n9, -9LL)
DIV64(p129, 129LL) DIV64(n129, -129LL)
DIV64(p7, 7LL) DIV64(n7, -7LL)
DIV64(p10, 10LL) DIV64(n10, -10LL)
DIV64(p37, 37LL) DIV64(n37, -37LL)
DIV64(pwide, 4294967297LL) DIV64(nwide, -4294967297LL)
DIV64(plarge, 4611686018427387905LL) DIV64(nlarge, -4611686018427387905LL)
DIV64(pmax, LMAX) DIV64(nmax, -LMAX)
NI int sd32_dynamic_q(int x, int d) { return x / d; }
NI int sd32_dynamic_r(int x, int d) { return x % d; }
NI long long sd64_dynamic_q(long long x, long long d) { return x / d; }
NI long long sd64_dynamic_r(long long x, long long d) { return x % d; }
NI long long sd32_cast_q(unsigned long long x) { return (int)x / 7; }
NI long long sd32_cast_r(unsigned long long x) { return (int)x % -7; }
NI int sd32_compound(int *p) { return *p /= -7; }
NI int sd32_compound_r(int *p) { return *p %= 10; }
NI long long sd64_compound(long long *p) { return *p /= -37LL; }
NI long long sd64_compound_r(long long *p) { return *p %= 7LL; }
static volatile unsigned effects;
static volatile unsigned index;
static volatile long long source;
static NI long long next(long long x) { effects++; return x; }
NI long long sd_effect(long long x) { return next(x) / -7LL; }
NI long long sd_effect_r(long long x) { return next(x) % 10LL; }
NI long long sd_rhs(long long x) { return x / (next(0), 37LL); }
NI long long sd_rhs_r(long long x) { return x % (next(0), -7LL); }
NI long long sd_volatile(void) { return source / -3LL; }
NI long long sd_volatile_compound(void) { return source /= -7LL; }
NI long long sd_volatile_r(void) { return source %= -3LL; }
NI long long sd_compound_rhs(long long *p) { return *p /= (next(0), -10LL); }
NI long long sd_index(long long *p) { return p[index++] %= -7LL; }
struct case32 { int d; int (*q)(int), (*r)(int); };
struct case64 { long long d; long long (*q)(long long), (*r)(long long); };
#define CASE32(name, divisor) { divisor, sd32_q_##name, sd32_r_##name }
#define CASE64(name, divisor) { divisor, sd64_q_##name, sd64_r_##name }
static const struct case32 cases32[] = {
    CASE32(p3, 3), CASE32(n3, -3), CASE32(p7, 7), CASE32(n7, -7),
    CASE32(p6, 6), CASE32(n6, -6), CASE32(p12, 12), CASE32(n12, -12),
    CASE32(p10, 10), CASE32(n10, -10), CASE32(p37, 37), CASE32(n37, -37),
    CASE32(plarge, 1073741825), CASE32(nlarge, -1073741825),
    CASE32(pmax, IMAX), CASE32(nmax, -IMAX),
};
static const struct case64 cases64[] = {
    CASE64(p3, 3LL), CASE64(n3, -3LL), CASE64(p7, 7LL), CASE64(n7, -7LL),
    CASE64(p6, 6LL), CASE64(n6, -6LL), CASE64(p12, 12LL), CASE64(n12, -12LL),
    CASE64(p9, 9LL), CASE64(n9, -9LL), CASE64(p129, 129LL), CASE64(n129, -129LL),
    CASE64(p10, 10LL), CASE64(n10, -10LL), CASE64(p37, 37LL), CASE64(n37, -37LL),
    CASE64(pwide, 4294967297LL), CASE64(nwide, -4294967297LL),
    CASE64(plarge, 4611686018427387905LL), CASE64(nlarge, -4611686018427387905LL),
    CASE64(pmax, LMAX), CASE64(nmax, -LMAX),
};
static unsigned long long random64(unsigned long long *s)
{
    unsigned long long x = *s;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17; *s = x;
    return x;
}
int main(void)
{
    unsigned long long seed = 0x123456789abcdefULL;
    static const int edge32[] = { IMIN, IMIN+1, -100, -38, -37, -36, -11, -10, -9, -8, -7, -6, -4, -3, -2, -1,
                                 0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 36, 37, 38, 100, IMAX-1, IMAX };
    static const long long edge64[] = { LMIN, LMIN+1, -100, -38, -37, -36, -11, -10, -9, -8, -7, -6, -4, -3, -2, -1,
                                       0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 36, 37, 38, 100, LMAX-1, LMAX };
    for (unsigned j = 0; j < sizeof(cases32)/sizeof(cases32[0]); j++) {
        const struct case32 *c = &cases32[j];
        for (unsigned i = 0; i < 72; i++) {
            int x;
            if (i < sizeof(edge32)/sizeof(edge32[0])) x = edge32[i];
            else if (i < 39) {
                /* Divisor and its negative, plus either neighbor: all fit int. */
                long long d = c->d;
                long long near = (i & 1) ? d : -d;
                near += (int)(i % 3) - 1;
                x = near < IMIN ? IMIN : near > IMAX ? IMAX : (int)near;
            } else x = (int)(unsigned)random64(&seed);
            int q = sd32_dynamic_q(x, c->d), r = sd32_dynamic_r(x, c->d);
            if (c->q(x) != q || c->r(x) != r) return 1;
        }
    }
    for (unsigned j = 0; j < sizeof(cases64)/sizeof(cases64[0]); j++) {
        const struct case64 *c = &cases64[j];
        for (unsigned i = 0; i < 72; i++) {
            long long x;
            if (i < sizeof(edge64)/sizeof(edge64[0])) x = edge64[i];
            else if (i < 39) {
                long long near = (i & 1) ? c->d : -c->d;
                int delta = (int)(i % 3) - 1;
                if (delta < 0 && near != LMIN) near--;
                if (delta > 0 && near != LMAX) near++;
                x = near;
            } else {
                x = (long long)(random64(&seed) & (unsigned long long)LMAX);
                if (i & 1) x = -x;
            }
            long long q = sd64_dynamic_q(x, c->d), r = sd64_dynamic_r(x, c->d);
            if (c->q(x) != q || c->r(x) != r) return 2;
        }
    }
    static const unsigned long long casts[] = { 0xdeadbeef80000000ULL, 0xdeadbeefffffffffULL,
                                               0xdeadbeef7fffffffULL, 0xdeadbeefffffff9cULL };
    for (unsigned i = 0; i < sizeof(casts)/sizeof(casts[0]); i++) {
        int x = (int)casts[i];
        if (sd32_cast_q(casts[i]) != sd32_dynamic_q(x, 7) ||
            sd32_cast_r(casts[i]) != sd32_dynamic_r(x, -7)) return 3;
    }
    int v = IMIN;
    if (sd32_compound(&v) != sd32_dynamic_q(IMIN, -7) || v != sd32_dynamic_q(IMIN, -7)) return 4;
    v = IMIN;
    if (sd32_compound_r(&v) != sd32_dynamic_r(IMIN, 10) || v != sd32_dynamic_r(IMIN, 10)) return 5;
    long long w = LMIN;
    if (sd64_compound(&w) != sd64_dynamic_q(LMIN, -37) || w != sd64_dynamic_q(LMIN, -37)) return 6;
    w = LMIN;
    if (sd64_compound_r(&w) != sd64_dynamic_r(LMIN, 7) || w != sd64_dynamic_r(LMIN, 7)) return 7;
    if (sd_effect(-100) != 14 || effects != 1 || sd_effect_r(-100) != 0 || effects != 2) return 8;
    if (sd_rhs(-100) != -2 || effects != 3 || sd_rhs_r(-100) != -2 || effects != 4) return 9;
    source = -100;
    if (sd_volatile() != 33 || source != -100) return 10;
    if (sd_volatile_compound() != 14 || source != 14 || sd_volatile_r() != 2 || source != 2) return 11;
    w = -100;
    if (sd_compound_rhs(&w) != 10 || w != 10 || effects != 5) return 12;
    long long indexed[2] = { -100, 77 };
    if (sd_index(indexed) != -2 || indexed[0] != -2 || indexed[1] != 77 || index != 1) return 13;
    return 0;
}
