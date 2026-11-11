/* Exact frame memory cells: forwarding and overwritten stores must preserve
 * operand evaluations, cell widths, aliases and observable operations. */
#define NI __attribute__((noinline))
struct pair { unsigned a, b; };
struct wide { unsigned long long a, b; };
struct floating { double a, b; };
static unsigned effects;

NI unsigned wc_forward(unsigned x)
{
    struct pair p;
    p.a = x;
    p.b = x + 3;
    return p.a + p.a + p.b;
}

NI unsigned wc_overwrite(unsigned x)
{
    struct pair p;
    p.a = x;
    p.a = x + 7;
    return p.a;
}

NI unsigned wc_load(unsigned x)
{
    struct pair p;
    p.a = x;
    __asm__ volatile ("" : : : "memory");
    return p.a + p.a + p.a;
}

NI unsigned long long wc_wide(unsigned long long x)
{
    struct wide p;
    p.a = x;
    p.b = x + 9;
    return p.a + p.b;
}

NI double wc_float(double x)
{
    struct floating p;
    p.a = x;
    p.b = x + 1.5;
    return p.a + p.b;
}

NI float wc_float32(float x)
{
    struct { float a, b; } p;
    p.a = x;
    p.b = x;
    return p.a;
}

NI unsigned wc_reinterpret(unsigned x)
{
    union { unsigned a; float b; } p;
    p.a = x;
    float first = p.b;
    p.a = 7;
    return __builtin_bit_cast(unsigned, first) + p.a;
}

NI unsigned wc_bits(unsigned x)
{
    struct pair p;
    p.a = 0x3f800000u;
    float first = __builtin_bit_cast(float, p.a);
    p.a = x;
    return first == 1.0f ? p.a : 99;
}

NI unsigned wc_volatile(unsigned x)
{
    struct { volatile unsigned a, b; } p;
    p.a = x;
    p.a = 7;
    p.b = 9; /* unread but observable */
    return p.a;
}

NI unsigned wc_atomic(unsigned x)
{
    struct { _Atomic unsigned a; unsigned b; } p;
    p.a = x;
    p.a = 7;
    return p.a;
}

static NI unsigned wc_effect(void) { return ++effects; }
NI unsigned wc_rhs(unsigned x)
{
    struct pair p;
    p.a = wc_effect();
    p.a = x;
    return p.a;
}

NI unsigned wc_union(unsigned x)
{
    union { unsigned a; unsigned char b[4]; } p;
    p.a = x;
    unsigned first = p.a;
    p.b[0] = 17;
    return first + p.b[0];
}

NI unsigned wc_bitfield(unsigned x)
{
    union { unsigned a; struct { unsigned lo:4, hi:28; } bits; } p;
    p.a = x;
    p.bits.lo = 3;
    return p.a;
}

NI unsigned wc_branch(unsigned x)
{
    struct pair p;
    p.a = x;
    if (x & 1) p.a = 7;
    else p.a = 11;
    return p.a;
}

NI unsigned wc_cond(unsigned x)
{
    struct pair p;
    p.a = 3;
    unsigned y = x ? (p.a = 7) : (p.a = 11);
    return y + p.a;
}

NI unsigned wc_loop(unsigned n)
{
    struct pair p;
    p.a = 3;
    p.b = 7;
    for (unsigned i = 0; i < n; ++i) {
        p.a += p.b;
        ++p.b;
    }
    return p.a + p.b;
}

NI unsigned wc_shortcircuit(unsigned x)
{
    struct pair p;
    p.a = 3;
    unsigned y = x && (p.a = 7);
    return y + p.a;
}

NI unsigned wc_dynamic(unsigned n)
{
    volatile unsigned v[n];
    struct pair p;
    p.a = n;
    v[n-1] = 17;
    return p.a + v[n-1];
}

static NI void wc_change(unsigned *p) { *p = 31; }
NI unsigned wc_escape(unsigned x)
{
    struct pair p;
    p.a = x;
    unsigned first = p.a;
    wc_change(&p.a);
    return first + p.a;
}

NI unsigned wc_narrow(unsigned x)
{
    struct { unsigned char a; signed char b; unsigned c; } p;
    p.a = x;
    p.b = -3;
    p.c = 7;
    return p.a + (unsigned)(p.b + 10) + p.c;
}

int main(void)
{
    for (unsigned i = 0; i < 16; ++i) {
        if (wc_forward(i) != 3*i+3 || wc_overwrite(i) != i+7) return 1;
        if (wc_load(i) != 3*i) return 2;
        if (wc_wide(0x1234567800000000ull + i) != 0x2468acf000000009ull + 2*i) return 3;
        if (wc_float((double)i) != 2*(double)i + 1.5) return 4;
        if (wc_volatile(i) != 7 || wc_atomic(i) != 7) return 5;
        if (wc_rhs(i) != i || effects != i+1) return 6;
        if (wc_union(i) != i+17 || wc_bitfield(i) != 3) return 7;
        if (wc_branch(i) != ((i&1) ? 7u : 11u)) return 8;
        if (wc_cond(i) != (i ? 14u : 22u)) return 9;
        if (wc_escape(i) != i+31 || wc_narrow(0x100+i) != i+14) return 10;
        if (wc_shortcircuit(i) != (i ? 8u : 3u) || wc_dynamic(i+1) != i+18) return 12;
        if (wc_bits(i) != i || wc_reinterpret(0x3f800000u+i) != 0x3f800007u+i) return 13;
    }
    unsigned bits[] = { 0, 0x80000000u, 0x3f800000u, 0x7fc12345u, 0xffc12345u };
    for (unsigned i = 0; i < sizeof(bits)/sizeof(*bits); ++i) {
        float value = __builtin_bit_cast(float, bits[i]);
        if (__builtin_bit_cast(unsigned, wc_float32(value)) != bits[i]) return 14;
    }
    if (wc_loop(0) != 10 || wc_loop(1) != 18 || wc_loop(4) != 48) return 11;
    return 0;
}
