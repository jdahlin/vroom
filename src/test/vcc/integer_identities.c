/* RHS integer identities: preserve evaluation, promotions and conversions. */
#define NI __attribute__((noinline))
NI int id_add(int x) { return x + 0; }
NI int id_sub(int x) { return x - 0; }
NI int id_mul(int x) { return x * 1; }
NI int id_div(int x) { return x / 1; }
NI int id_or(int x) { return x | 0; }
NI int id_xor(int x) { return x ^ 0; }
NI int id_and(int x) { return x & -1; }
NI int id_shl(int x) { return x << 0; }
NI int id_shr(int x) { return x >> 0; }
NI int id_mulzero(int x) { return x * 0; }
NI int id_andzero(int x) { return x & 0; }
NI int id_remone(int x) { return x % 1; }
NI int id_orones(int x) { return x | -1; }
NI unsigned id_uand(unsigned x) { return x & 0xffffffffu; }
NI unsigned id_uorones(unsigned x) { return x | 0xffffffffu; }
NI unsigned long long id_umul64(unsigned long long x) { return x * 1ull; }
NI unsigned long long id_uand64(unsigned long long x) { return x & 0xffffffffffffffffull; }
NI unsigned long long id_uorones64(unsigned long long x) { return x | 0xffffffffffffffffull; }
NI unsigned long long id_uremone64(unsigned long long x) { return x % 1ull; }
NI long long id_sdiv64(long long x) { return x / 1ll; }
NI long long id_sremone64(long long x) { return x % 1ll; }
volatile unsigned id_input = 31;
static volatile unsigned id_calls;
NI unsigned id_next(void) { id_calls++; return id_input++; }
NI unsigned id_effect_mulzero(void) { return id_next() * 0u; }
NI unsigned id_effect_andzero(void) { return id_next() & 0u; }
NI unsigned id_effect_remone(void) { return id_next() % 1u; }
NI unsigned id_effect_orones(void) { return id_next() | 0xffffffffu; }
NI unsigned id_effect_mulone(void) { return id_next() * 1u; }
NI unsigned id_rhs_add(unsigned x) { return x + (id_next(), 0u); }
NI unsigned id_rhs_mulzero(unsigned x) { return x * (id_next(), 0u); }
NI unsigned id_rhs_andones(unsigned x) { return x & (id_next(), 0xffffffffu); }
NI unsigned id_volatile_zero(void) { return id_input * 0u; }
NI unsigned id_volatile_compound(void) { return id_input *= 0u; }
NI unsigned id_compound(unsigned x) { x *= 1u; x |= 0u; x ^= 0u; x &= 0xffffffffu; return x; }
NI unsigned char id_compound_small(unsigned char x) { x *= 1; x += 0; x /= 1; return x; }
NI signed char id_compound_signed(signed char x) { x |= 0; x &= -1; x %= 1; return x; }
NI unsigned long long id_unsigned_widen(unsigned long long x) { return (unsigned)x + 0u; }
NI long long id_signed_widen(unsigned long long x) { return (int)x + 0; }
NI unsigned short id_compound_short(unsigned short x) { x += 0; x &= 0xffffffffu; return x; }
int main(void)
{
    if (id_add(-31) != -31 || id_sub(-31) != -31) return 1;
    if (id_mul(-31) != -31 || id_div(-31) != -31) return 2;
    if (id_or(-31) != -31 || id_xor(-31) != -31 || id_and(-31) != -31) return 3;
    if (id_shl(31) != 31 || id_shr(-31) != -31) return 4;
    if (id_mulzero(-31) != 0 || id_andzero(-31) != 0 || id_remone(-31) != 0) return 5;
    if (id_orones(31) != -1 || id_uand(0x80000000u) != 0x80000000u) return 6;
    if (id_uorones(31) != 0xffffffffu) return 7;
    if (id_umul64(0x8000000000000000ull) != 0x8000000000000000ull) return 8;
    if (id_uand64(0x8000000000000000ull) != 0x8000000000000000ull) return 9;
    if (id_uorones64(0) != 0xffffffffffffffffull || id_uremone64(0xffffffffffffffffull) != 0) return 10;
    if (id_sdiv64(-9223372036854775807ll - 1) != (-9223372036854775807ll - 1) || id_sremone64(-31) != 0) return 11;
    if (id_effect_mulzero() != 0 || id_calls != 1 || id_input != 32) return 12;
    if (id_effect_andzero() != 0 || id_calls != 2 || id_input != 33) return 13;
    if (id_effect_remone() != 0 || id_calls != 3 || id_input != 34) return 14;
    if (id_effect_orones() != 0xffffffffu || id_calls != 4 || id_input != 35) return 15;
    if (id_effect_mulone() != 35 || id_calls != 5 || id_input != 36) return 16;
    if (id_rhs_add(31) != 31 || id_calls != 6 || id_input != 37) return 17;
    if (id_rhs_mulzero(31) != 0 || id_calls != 7 || id_input != 38) return 18;
    if (id_rhs_andones(31) != 31 || id_calls != 8 || id_input != 39) return 19;
    if (id_volatile_zero() != 0 || id_input != 39) return 20;
    if (id_compound(0xffffffffu) != 0xffffffffu || id_compound_small(255) != 255 || id_compound_signed(-31) != 0) return 21;
    if (id_unsigned_widen(0x12345678ffffffffull) != 0xffffffffull) return 22;
    if (id_unsigned_widen(0x1234567880000000ull) != 0x80000000ull) return 23;
    if (id_signed_widen(0x12345678ffffffffull) != -1ll) return 24;
    if (id_signed_widen(0x12345678ffffffe1ull) != -31ll) return 25;
    if (id_signed_widen(0x1234567880000000ull) != (-2147483647ll - 1)) return 26;
    if (id_compound_short(65535) != 65535) return 27;
    if (id_volatile_compound() != 0 || id_input != 0) return 29;
    if (id_div(-2147483647 - 1) != (-2147483647 - 1) || id_remone(-2147483647 - 1) != 0) return 28;
    return 0;
}
