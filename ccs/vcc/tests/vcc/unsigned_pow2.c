/* Unsigned power-of-two lowering: literal answers, boundaries and effects.
 * The opt-rules harness also inspects the externally visible probes below. */
#define NOINLINE __attribute__((noinline))
NOINLINE unsigned opt_u32_div8(unsigned x) { return x / 8u; }
NOINLINE unsigned opt_u32_rem8(unsigned x) { return x % 8u; }
NOINLINE unsigned opt_u32_divhigh(unsigned x) { return x / 0x80000000u; }
NOINLINE unsigned opt_u32_remhigh(unsigned x) { return x % 0x80000000u; }
NOINLINE unsigned opt_u32_div1(unsigned x) { return x / 1u; }
NOINLINE unsigned opt_u32_rem1(unsigned x) { return x % 1u; }
NOINLINE unsigned long long opt_u64_div8(unsigned long long x) { return x / 8ull; }
NOINLINE unsigned long long opt_u64_rem8(unsigned long long x) { return x % 8ull; }
NOINLINE unsigned long long opt_u64_divhigh(unsigned long long x) { return x / 0x8000000000000000ull; }
NOINLINE unsigned long long opt_u64_remhigh(unsigned long long x) { return x % 0x8000000000000000ull; }
NOINLINE unsigned opt_cast_div(unsigned x) { return x / (unsigned char)8; }
NOINLINE unsigned opt_cast_rem(unsigned x) { return x % (unsigned short)8; }
NOINLINE unsigned opt_truncated_div(unsigned x) { return x / (unsigned)0x100000008ull; }
NOINLINE unsigned opt_signed_literal_div(unsigned x) { return x / (-2147483647 - 1); }
NOINLINE unsigned opt_compound_div(unsigned x) { x /= 8u; return x; }
NOINLINE unsigned opt_compound_rem(unsigned x) { x %= 8u; return x; }
NOINLINE unsigned long long opt_compound_div64(unsigned long long x) { x /= 8ull; return x; }
NOINLINE unsigned long long opt_compound_rem64(unsigned long long x) { x %= 8ull; return x; }
/* These controls must retain their original C semantics. */
NOINLINE int signed_div(int x) { return x / 8; }
NOINLINE int signed_rem(int x) { return x % 8; }
NOINLINE unsigned other_div(unsigned x) { return x / 7u; }
NOINLINE unsigned other_rem(unsigned x) { return x % 7u; }
NOINLINE unsigned dynamic_div(unsigned x, unsigned d) { return x / d; }
NOINLINE unsigned dynamic_rem(unsigned x, unsigned d) { return x % d; }
static volatile unsigned source = 31, calls;
NOINLINE unsigned next(void) { calls++; return source++; }
NOINLINE unsigned effect_div(void) { return next() / 8u; }
NOINLINE unsigned effect_rem(void) { return next() % 8u; }
NOINLINE unsigned effect_zero(void) { return next() % 1u; }
NOINLINE unsigned effect_denominator(unsigned x) { return x / (next(), 8u); }
NOINLINE unsigned effect_remdenominator(unsigned x) { return x % (next(), 8u); }
int main(void)
{
    if (opt_u32_div8(0) != 0 || opt_u32_rem8(0) != 0) return 1;
    if (opt_u32_div8(7) != 0 || opt_u32_rem8(7) != 7) return 2;
    if (opt_u32_div8(8) != 1 || opt_u32_rem8(8) != 0) return 3;
    if (opt_u32_div8(0xffffffffu) != 536870911u || opt_u32_rem8(0xffffffffu) != 7) return 4;
    if (opt_u32_divhigh(0x7fffffffu) != 0 || opt_u32_remhigh(0x7fffffffu) != 0x7fffffffu) return 5;
    if (opt_u32_divhigh(0x80000000u) != 1 || opt_u32_remhigh(0x80000000u) != 0) return 6;
    if (opt_u32_divhigh(0xffffffffu) != 1 || opt_u32_remhigh(0xffffffffu) != 0x7fffffffu) return 7;
    if (opt_u32_div1(0xffffffffu) != 0xffffffffu || opt_u32_rem1(0xffffffffu) != 0) return 8;
    if (opt_u64_div8(0xffffffffffffffffull) != 2305843009213693951ull || opt_u64_rem8(0xffffffffffffffffull) != 7) return 9;
    if (opt_u64_divhigh(0x7fffffffffffffffull) != 0 || opt_u64_remhigh(0x7fffffffffffffffull) != 0x7fffffffffffffffull) return 10;
    if (opt_u64_divhigh(0x8000000000000000ull) != 1 || opt_u64_remhigh(0x8000000000000000ull) != 0) return 11;
    if (opt_u64_divhigh(0xffffffffffffffffull) != 1 || opt_u64_remhigh(0xffffffffffffffffull) != 0x7fffffffffffffffull) return 12;
    if (opt_cast_div(31) != 3 || opt_cast_rem(31) != 7) return 13;
    if (opt_truncated_div(31) != 3 || opt_signed_literal_div(0xffffffffu) != 1) return 24;
    if (signed_div(-31) != -3 || signed_rem(-31) != -7) return 14;
    if (other_div(31) != 4 || other_rem(31) != 3) return 15;
    if (dynamic_div(31, 8) != 3 || dynamic_rem(31, 8) != 7) return 16;
    if (effect_div() != 3 || calls != 1 || source != 32) return 17;
    if (effect_rem() != 0 || calls != 2 || source != 33) return 18;
    if (effect_zero() != 0 || calls != 3 || source != 34) return 19;
    if (effect_denominator(31) != 3 || calls != 4 || source != 35) return 20;
    if (effect_remdenominator(31) != 7 || calls != 5 || source != 36) return 21;
    { unsigned x = 31; x /= 8u; if (x != 3) return 22; x = 31; x %= 8u; if (x != 7) return 23; }
    if (opt_compound_div(31) != 3 || opt_compound_rem(31) != 7) return 25;
    if (opt_compound_div64(0xffffffffffffffffull) != 2305843009213693951ull ||
        opt_compound_rem64(0xffffffffffffffffull) != 7) return 26;
    return 0;
}
