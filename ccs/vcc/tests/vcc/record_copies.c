/* Forwarding a register copy must retain widths, implicit operands and edges. */
#define NI __attribute__((noinline))
typedef unsigned long long U;
static volatile unsigned cp_calls;

/* Several stores reuse the same pointer and value. Native copy propagation
   can feed those operands directly from their homes. */
NI long long cp_stores(long long *p, long long x)
{
    p[0] = x;
    p[1] = x + 2;
    p[2] = x + 3;
    return p[0];
}
NI U cp_shift(U x, unsigned n)
{
    U saved = x;
    return (saved << n) ^ (x >> (n & 31));
}
NI U cp_div(U x, U d)
{
    U saved = x;
    return (saved / d) * 17 + x % d;
}
NI long long cp_sdiv(long long x, long long d)
{
    long long saved = x;
    return (saved / d) * 17 + x % d;
}
NI U cp_narrow(U x)
{
    unsigned low = (unsigned)x;
    unsigned char byte = (unsigned char)x;
    unsigned short half = (unsigned short)(x >> 8);
    return ((U)low << 32) ^ ((U)byte << 16) ^ half;
}
NI long long cp_signed_narrow(U x)
{
    signed char byte = (signed char)x;
    short half = (short)(x >> 8);
    return (long long)byte * 65536 + half;
}
NI U cp_source(U x, U y)
{
    U saved = x;
    x += y;
    y ^= saved;
    return saved + x * 3 + y * 5;
}
NI U cp_helper(U x) { ++cp_calls; return x + 19; }
NI U cp_call(U x)
{
    U saved = x;
    U result = cp_helper(x + 1);
    return saved * 3 + result;
}
NI U cp_edges(U x, int n)
{
    U saved = x;
    if (n < 0) goto negative;
    if (n) x += 7;
    else x ^= 3;
    goto done;
negative:
    x *= 5;
done:
    return saved + x;
}
NI U cp_loop(U x, unsigned n)
{
    U saved = x;
    while (n--) {
        U old = x;
        x = old * 3 + saved;
    }
    return saved ^ x;
}
NI U cp_overflow(unsigned x, unsigned y)
{
    unsigned sum;
    int carry = __builtin_add_overflow(x, y, &sum);
    return ((U)(unsigned)carry << 32) | sum;
}
NI U cp_atomic(unsigned *p, unsigned x)
{
    unsigned before = __atomic_fetch_add(p, x, __ATOMIC_SEQ_CST);
    unsigned expected = before + x;
    int changed = __atomic_compare_exchange_n(p, &expected, 37, 0,
                                               __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    unsigned exchanged = __atomic_exchange_n(p, 41, __ATOMIC_SEQ_CST);
    return ((U)before << 32) | ((U)(unsigned)changed << 24) | exchanged;
}
#if defined(__x86_64__) || defined(__aarch64__)
NI U cp_asm(U x, U y)
{
    U saved = x;
#if defined(__x86_64__)
    __asm__ volatile("addq $5, %0" : "+r"(x) : : "cc", "memory");
#else
    __asm__ volatile("add %0, %0, #5" : "+r"(x) : : "memory");
#endif
    return saved * 3 + x + y;
}
#endif
int main(void)
{
    long long slots[3];
    if (cp_stores(slots, -11) != -11 || slots[0] != -11 || slots[1] != -9 || slots[2] != -8) return 1;
    if (cp_shift(0xfedcba9876543210ULL, 4) != 0xe226622ee2266221ULL) return 2;
    if (cp_shift(0x123456789abcdef0ULL, 33) != 0x3c6396dc4d5e6f78ULL) return 3;
    if (cp_div(1001, 13) != 1309 || cp_div(0xffffffffffffffffULL, 0x100000001ULL) != 0x10ffffffefULL) return 4;
    if (cp_sdiv(-1001, 13) != -1309 || cp_sdiv(1001, -13) != -1309) return 5;
    if (cp_narrow(0xfedcba987654abcdULL) != 0x7654abcd00cd54abULL) return 6;
    if (cp_signed_narrow(0x80ffULL) != -65408 || cp_signed_narrow(0xff8001ULL) != 65408) return 7;
    if (cp_source(11, 7) != 125 || cp_source(0xffffffffffffffffULL, 2) != 0xfffffffffffffff3ULL) return 8;
    if (cp_call(11) != 64 || cp_calls != 1) return 9;
    if (cp_edges(11, -1) != 66 || cp_edges(11, 0) != 19 || cp_edges(11, 1) != 29) return 10;
    if (cp_loop(2, 0) != 0 || cp_loop(2, 3) != 82) return 11;
    if (cp_overflow(0xffffffffU, 7) != 0x100000006ULL || cp_overflow(11, 7) != 18) return 12;
    unsigned atom = 11;
    if (cp_atomic(&atom, 7) != 0xb01000025ULL || atom != 41) return 13;
#if defined(__x86_64__) || defined(__aarch64__)
    if (cp_asm(11, 7) != 56) return 14;
#endif
    return 0;
}
