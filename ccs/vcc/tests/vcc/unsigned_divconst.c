/* Unsigned32 constant-divisor semantics and exact evaluation counts. */
#define NI __attribute__((noinline))
static volatile unsigned effects;
static volatile unsigned source;
static volatile unsigned index;
static NI unsigned next(unsigned x) { effects++; return x; }
NI unsigned ud_q3(unsigned x) { return x / 3u; }
NI unsigned ud_r3(unsigned x) { return x % 3u; }
NI unsigned ud_q5(unsigned x) { return x / 5u; }
NI unsigned ud_r5(unsigned x) { return x % 5u; }
NI unsigned ud_q6(unsigned x) { return x / 6u; }
NI unsigned ud_r6(unsigned x) { return x % 6u; }
NI unsigned ud_q7(unsigned x) { return x / 7u; }
NI unsigned ud_r7(unsigned x) { return x % 7u; }
NI unsigned ud_q9(unsigned x) { return x / 9u; }
NI unsigned ud_r9(unsigned x) { return x % 9u; }
NI unsigned ud_q10(unsigned x) { return x / 10u; }
NI unsigned ud_r10(unsigned x) { return x % 10u; }
NI unsigned ud_q11(unsigned x) { return x / 11u; }
NI unsigned ud_r11(unsigned x) { return x % 11u; }
NI unsigned ud_q14(unsigned x) { return x / 14u; }
NI unsigned ud_r14(unsigned x) { return x % 14u; }
NI unsigned ud_q19(unsigned x) { return x / 19u; }
NI unsigned ud_r19(unsigned x) { return x % 19u; }
NI unsigned ud_q31(unsigned x) { return x / 31u; }
NI unsigned ud_r31(unsigned x) { return x % 31u; }
NI unsigned ud_q37(unsigned x) { return x / 37u; }
NI unsigned ud_r37(unsigned x) { return x % 37u; }
NI unsigned ud_q1000(unsigned x) { return x / 1000u; }
NI unsigned ud_r1000(unsigned x) { return x % 1000u; }
NI unsigned ud_q65535(unsigned x) { return x / 65535u; }
NI unsigned ud_r65535(unsigned x) { return x % 65535u; }
NI unsigned ud_q65537(unsigned x) { return x / 65537u; }
NI unsigned ud_r65537(unsigned x) { return x % 65537u; }
NI unsigned ud_q2147483647(unsigned x) { return x / 2147483647u; }
NI unsigned ud_r2147483647(unsigned x) { return x % 2147483647u; }
NI unsigned ud_q2147483649(unsigned x) { return x / 2147483649u; }
NI unsigned ud_r2147483649(unsigned x) { return x % 2147483649u; }
NI unsigned ud_q4294967295(unsigned x) { return x / 4294967295u; }
NI unsigned ud_r4294967295(unsigned x) { return x % 4294967295u; }
NI unsigned ud_truncated(unsigned x) { return x / (unsigned)0x100000003ULL; }
NI unsigned ud_negative_literal(unsigned x) { return x / -3; }
NI unsigned ud_cast(unsigned long long x) { return (unsigned)x / 7u; }
NI unsigned ud_castrem(unsigned long long x) { return (unsigned)x % 7u; }
NI unsigned ud_compound(unsigned *p) { return (*p /= 10u); }
NI unsigned ud_compoundrem(unsigned *p) { return (*p %= 7u); }
NI unsigned ud_effect(unsigned x) { return next(x) / 3u; }
NI unsigned ud_effectrem(unsigned x) { return next(x) % 7u; }
NI unsigned ud_rhs(unsigned x) { return x / (next(0), 10u); }
NI unsigned ud_rhsrem(unsigned x) { return x % (next(0), 7u); }
NI unsigned ud_volatile(void) { return source / 3u; }
NI unsigned ud_volatile_compound(void) { return source /= 10u; }
NI unsigned ud_volatile_rem(void) { return source %= 7u; }
NI unsigned ud_compound_rhs(unsigned *p) { return (*p /= (next(0), 10u)); }
NI unsigned ud_index(unsigned *p) { return p[index++] /= 10u; }
NI unsigned ud_dynamic(unsigned n, unsigned d) { return n / d; }
NI unsigned long long ud_wide(unsigned long long n) { return n / 7ULL; }
NI int ud_signed(int n) { return n / 7; }
static unsigned pressure_anchor;
static NI unsigned pressure_next(unsigned n) { return n + source; }
NI unsigned ud_pressure(void) { return pressure_next(14u) + (pressure_next(13u) + (pressure_next(12u) + (pressure_next(11u) + (pressure_next(10u) + (pressure_next(9u) + (pressure_next(8u) + (pressure_next(7u) + (pressure_next(6u) + (pressure_next(5u) + (pressure_next(4u) + (pressure_next(3u) + (pressure_next(2u) + (pressure_next(1u) + (pressure_next(0u) + (((unsigned)(unsigned long)&pressure_anchor % 7u)))))))))))))))); }
static unsigned random32(unsigned *s) { unsigned x=*s; x^=x<<13; x^=x>>17; x^=x<<5; *s=x; return x; }
int main(void)
{
    unsigned seed=0x12345678;
    for (unsigned i=0;i<256;i++) {
        unsigned x = i<8 ? (unsigned[]){0,1,2,3,0x7fffffffU,0x80000000U,0xfffffffeU,0xffffffffU}[i] : random32(&seed);
        if (ud_q3(x) != ud_dynamic(x, 3u) || ud_r3(x) != x - ud_dynamic(x, 3u) * 3u) return 1;
        if (ud_q5(x) != ud_dynamic(x, 5u) || ud_r5(x) != x - ud_dynamic(x, 5u) * 5u) return 1;
        if (ud_q6(x) != ud_dynamic(x, 6u) || ud_r6(x) != x - ud_dynamic(x, 6u) * 6u) return 1;
        if (ud_q7(x) != ud_dynamic(x, 7u) || ud_r7(x) != x - ud_dynamic(x, 7u) * 7u) return 1;
        if (ud_q9(x) != ud_dynamic(x, 9u) || ud_r9(x) != x - ud_dynamic(x, 9u) * 9u) return 1;
        if (ud_q10(x) != ud_dynamic(x, 10u) || ud_r10(x) != x - ud_dynamic(x, 10u) * 10u) return 1;
        if (ud_q11(x) != ud_dynamic(x, 11u) || ud_r11(x) != x - ud_dynamic(x, 11u) * 11u) return 1;
        if (ud_q14(x) != ud_dynamic(x, 14u) || ud_r14(x) != x - ud_dynamic(x, 14u) * 14u) return 1;
        if (ud_q19(x) != ud_dynamic(x, 19u) || ud_r19(x) != x - ud_dynamic(x, 19u) * 19u) return 1;
        if (ud_q31(x) != ud_dynamic(x, 31u) || ud_r31(x) != x - ud_dynamic(x, 31u) * 31u) return 1;
        if (ud_q37(x) != ud_dynamic(x, 37u) || ud_r37(x) != x - ud_dynamic(x, 37u) * 37u) return 1;
        if (ud_q1000(x) != ud_dynamic(x, 1000u) || ud_r1000(x) != x - ud_dynamic(x, 1000u) * 1000u) return 1;
        if (ud_q65535(x) != ud_dynamic(x, 65535u) || ud_r65535(x) != x - ud_dynamic(x, 65535u) * 65535u) return 1;
        if (ud_q65537(x) != ud_dynamic(x, 65537u) || ud_r65537(x) != x - ud_dynamic(x, 65537u) * 65537u) return 1;
        if (ud_q2147483647(x) != ud_dynamic(x, 2147483647u) || ud_r2147483647(x) != x - ud_dynamic(x, 2147483647u) * 2147483647u) return 1;
        if (ud_q2147483649(x) != ud_dynamic(x, 2147483649u) || ud_r2147483649(x) != x - ud_dynamic(x, 2147483649u) * 2147483649u) return 1;
        if (ud_q4294967295(x) != ud_dynamic(x, 4294967295u) || ud_r4294967295(x) != x - ud_dynamic(x, 4294967295u) * 4294967295u) return 1;
        if (ud_truncated(x) != ud_dynamic(x,3) || ud_negative_literal(x) != ud_dynamic(x,4294967293U)) return 13;
        unsigned long long wide=0xfedcba9800000000ULL | x;
        if (ud_cast(wide) != ud_dynamic(x,7) || ud_castrem(wide) != x-ud_dynamic(x,7)*7) return 2;
        unsigned value=x;
        if (ud_compound(&value) != ud_dynamic(x,10) || value != ud_dynamic(x,10)) return 3;
        value=x;
        if (ud_compoundrem(&value) != x-ud_dynamic(x,7)*7 || value != x-ud_dynamic(x,7)*7) return 4;
    }
    if (ud_effect(100) != 33 || effects != 1 || ud_effectrem(100) != 2 || effects != 2) return 5;
    if (ud_rhs(100) != 10 || effects != 3 || ud_rhsrem(100) != 2 || effects != 4) return 6;
    source=100;
    if (ud_volatile() != 33 || source != 100) return 7;
    if (ud_volatile_compound() != 10 || source != 10 || ud_volatile_rem() != 3 || source != 3) return 8;
    unsigned value=100;
    if (ud_compound_rhs(&value) != 10 || value != 10 || effects != 5) return 9;
    unsigned indexed[2]={100,77};
    if (ud_index(indexed) != 10 || indexed[0] != 10 || indexed[1] != 77 || index != 1) return 11;
    unsigned address=(unsigned)(unsigned long)&pressure_anchor;
    if (ud_pressure() != 105 + 15*source + address-ud_dynamic(address,7)*7) return 12;
    if (ud_wide(0xffffffffffffffffULL) != 2635249153387078802ULL || ud_signed(-100) != -14) return 10;
    return 0;
}
