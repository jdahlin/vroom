/* Cross-join dead frame stores: private aggregate cells and structured flow. */
#define NI __attribute__((noinline))
struct ints { unsigned a, b; };
struct wides { unsigned long long a, b; };
struct floats { float a, b; };
struct doubles { double a, b; };
static volatile unsigned effects;
static volatile unsigned observed;
static volatile unsigned epoch_observed;
static NI unsigned wf_effect(unsigned x) { effects++; return x; }
static NI void wf_barrier(void) { observed++; }
static NI void wf_epoch_barrier(void) { epoch_observed++; }
static NI void wf_change(unsigned *p) { *p = 31; }
NI unsigned wf_diamond(unsigned c, unsigned x)
{
    struct ints p;
    p.a = 1;
    if (c) p.a = 2;
    else p.a = 3;
    p.a = x;
    return p.a;
}
/* The empty else makes the final control fence precede the join. A call
 * barrier after the join must not be coalesced into that else-only event. */
NI unsigned wf_epoch_empty_else(unsigned c, unsigned x)
{
    struct ints p;
    p.a = x;
    if (c) p.a = 2;
    else { }
    wf_epoch_barrier();
    p.a = 7;
    return p.a;
}
/* Mirror the empty arm: the post-join call remains a barrier on both paths. */
NI unsigned wf_epoch_empty_then(unsigned c, unsigned x)
{
    struct ints p;
    p.a = x;
    if (c) { }
    else p.a = 2;
    wf_epoch_barrier();
    p.a = 7;
    return p.a;
}
NI unsigned wf_nested(unsigned c, unsigned d, unsigned x)
{
    struct ints p;
    p.a = 1;
    if (c) {
        if (d) p.a = 2;
        else p.a = 3;
    } else p.a = 4;
    p.a = x;
    return p.a;
}
NI unsigned wf_block(unsigned c, unsigned x)
{
    struct ints p;
    p.a = 1;
    { if (c) { p.a = 2; } else { p.a = 3; } }
    p.a = x;
    return p.a;
}
NI unsigned long long wf_wide(unsigned c, unsigned long long x)
{
    struct wides p;
    p.a = 1;
    if (c) p.a = 2;
    else p.a = 3;
    p.a = x;
    return p.a;
}
NI float wf_float(unsigned c, float x)
{
    struct floats p;
    p.a = 1.0f;
    if (c) p.a = 2.0f;
    else p.a = 3.0f;
    p.a = x;
    return p.a;
}
NI double wf_double(unsigned c, double x)
{
    struct doubles p;
    p.a = 1.0;
    if (c) p.a = 2.0;
    else p.a = 3.0;
    p.a = x;
    return p.a;
}
NI unsigned wf_rhs(unsigned c, unsigned x)
{
    struct ints p;
    p.a = wf_effect(1);
    if (c) p.a = wf_effect(2);
    else p.a = wf_effect(3);
    p.a = x;
    return p.a;
}
NI unsigned wf_read(unsigned c, unsigned x)
{
    struct ints p;
    p.a = x;
    unsigned first;
    if (c) { first = p.a; p.a = 7; }
    else { first = p.a + 1; p.a = 9; }
    unsigned second = p.a;
    p.a = 11;
    return first + second + p.a;
}
NI unsigned wf_exit(unsigned c, unsigned x)
{
    struct ints p;
    p.a = x;
    if (c) return p.a + 3;
    p.a = 7;
    return p.a;
}
NI unsigned wf_call(unsigned c, unsigned x)
{
    struct ints p;
    p.a = x;
    if (c) wf_barrier();
    else wf_barrier();
    p.a = 7;
    return p.a;
}
NI unsigned wf_escape(unsigned c, unsigned x)
{
    struct ints p;
    p.a = x;
    if (c) wf_change(&p.a);
    else p.a = 9;
    unsigned first = p.a;
    p.a = 7;
    return first + p.a;
}
NI unsigned wf_volatile(unsigned c, unsigned x)
{
    struct { volatile unsigned a, b; } p;
    p.a = x;
    if (c) p.a = 3;
    else p.a = 5;
    p.a = 7;
    return p.a;
}
NI unsigned wf_atomic(unsigned c, unsigned x)
{
    struct { _Atomic unsigned a; unsigned b; } p;
    p.a = x;
    if (c) p.a = 3;
    else p.a = 5;
    p.a = 7;
    return p.a;
}
NI unsigned wf_overlap(unsigned c, unsigned x)
{
    union { unsigned word; unsigned char byte[4]; } p;
    p.word = x;
    if (c) p.byte[0] = 17;
    else p.byte[0] = 19;
    unsigned first = p.word;
    p.word = 7;
    return first + p.word;
}
NI unsigned wf_loop(unsigned n, unsigned x)
{
    struct ints p;
    p.a = x;
    for (unsigned i = 0; i < n; i++) p.a += i;
    unsigned first = p.a;
    p.a = 7;
    return first + p.a;
}
NI unsigned wf_switch(unsigned c, unsigned x)
{
    struct ints p;
    p.a = x;
    switch (c) { case 0: p.a += 1; break; case 1: p.a += 2; break; default: p.a += 3; }
    unsigned first = p.a;
    p.a = 7;
    return first + p.a;
}
NI unsigned wf_goto(unsigned c, unsigned x)
{
    struct ints p;
    p.a = x;
    if (c) goto done;
    p.a += 9;
done:
    { unsigned first = p.a; p.a = 7; return first + p.a; }
}
NI unsigned wf_vla(unsigned n, unsigned x)
{
    volatile unsigned v[n];
    struct ints p;
    p.a = x;
    v[n-1] = 11;
    if (n & 1) p.a += v[n-1];
    else p.a += 3;
    unsigned first = p.a;
    p.a = 7;
    return first + p.a;
}
int main(void)
{
    for (unsigned c = 0; c < 4; c++) {
        unsigned x = c + 100;
        if (wf_diamond(c, x) != x || wf_nested(c, c & 1, x) != x || wf_block(c, x) != x) return 1;
        if (wf_epoch_empty_else(c, x) != 7 || wf_epoch_empty_then(c, x) != 7 ||
            epoch_observed != 2 * (c + 1)) return 13;
        unsigned long long wide = 0x1234567800000000ULL + x;
        if (wf_wide(c, wide) != wide || wf_float(c, (float)x) != (float)x || wf_double(c, (double)x) != (double)x) return 2;
        if (wf_rhs(c, x) != x || effects != 2 * (c + 1)) return 3;
        if (wf_read(c, x) != x + (c ? 18 : 21) || wf_exit(c, x) != (c ? x + 3 : 7)) return 4;
        if (wf_call(c, x) != 7 || observed != c + 1 || wf_escape(c, x) != (c ? 38 : 16)) return 5;
        if (wf_volatile(c, x) != 7 || wf_atomic(c, x) != 7) return 6;
        /* The supported targets are little-endian. */
        if (wf_overlap(c, x) != ((x & ~255u) | (c ? 17u : 19u)) + 7) return 7;
        if (wf_loop(c, x) != x + c * (c - 1) / 2 + 7) return 8;
        if (wf_switch(c, x) != x + (c < 2 ? c + 1 : 3) + 7) return 9;
        if (wf_goto(c, x) != x + (c ? 0 : 9) + 7 || wf_vla(c+1, x) != x + ((c+1) & 1 ? 18 : 10)) return 10;
    }
    unsigned floatbits[] = { 0, 0x80000000u, 0x7fc12345u };
    unsigned long long doublebits[] = { 0, 0x8000000000000000ULL, 0x7ff8123456789abcULL };
    for (unsigned i = 0; i < 3; i++) {
        float f = __builtin_bit_cast(float, floatbits[i]);
        double d = __builtin_bit_cast(double, doublebits[i]);
        if (__builtin_bit_cast(unsigned, wf_float(i, f)) != floatbits[i]) return 11;
        if (__builtin_bit_cast(unsigned long long, wf_double(i, d)) != doublebits[i]) return 12;
    }
    return 0;
}
