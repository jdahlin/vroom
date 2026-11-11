/* Dead pure results may disappear; effects, flag users and ABI results stay. */
#define NI __attribute__((noinline))
typedef unsigned long long U;
static volatile unsigned dr_seen;
NI unsigned dr_mul(unsigned x) { return (x * 13u) & 0u; }
NI U dr_mul64(U x) { return (x * 37ULL) & 0ULL; }
NI unsigned dr_zero_left(unsigned x) { return 0u * (x * 13u); }
NI unsigned dr_ones(unsigned x) { return (x * 13u) | ~0u; }
NI unsigned dr_rem_one(unsigned x) { return (x * 13u) % 1u; }
NI unsigned dr_zero_shift(unsigned x) { return 0u << (x & 31); }
NI unsigned dr_diamond(unsigned x, int condition)
{
    if (condition) return (x * 13u) & 0u;
    else return (x * 17u) & 0u;
}
NI unsigned dr_chain(unsigned x, unsigned y)
{
    return (((x * 13u) ^ (y * 17u)) + 19u) & 0u;
}
NI unsigned dr_returned(unsigned x) { return x * 13u + 17u; }
NI unsigned dr_div(unsigned x, unsigned d) { return (x / d) & 0u; }
NI unsigned dr_remainder(unsigned x, unsigned d) { return (x % d) & 0u; }
NI unsigned dr_volatile(volatile unsigned *p) { return (*p * 13u) & 0u; }
NI unsigned dr_atomic(unsigned *p) { return (__atomic_fetch_add(p, 7, 5) * 13u) & 0u; }
NI unsigned dr_overflow(unsigned x, unsigned y)
{
    unsigned discarded;
    return __builtin_add_overflow(x, y, &discarded);
}
NI U dr_carry(U a, U b, U carry)
{
    U sum = a + b, carry1 = sum < a;
    U result = sum + carry, carry2 = result < sum;
    return (carry1 | carry2) + result;
}
NI U dr_partial(U x, unsigned y)
{
    unsigned char byte = (unsigned char)y;
    unsigned short half = (unsigned short)(y >> 8);
    return x ^ ((U)byte << 40) ^ half;
}
typedef struct { U low, high; } dr_pair;
NI dr_pair dr_pair_result(U x, U y)
{
    dr_pair result = { x * 13, y * 17 };
    return result;
}
typedef struct { double first, second; } dr_fp_pair;
NI dr_fp_pair dr_fp_result(double x, double y)
{
    dr_fp_pair result = { x + 1.5, y / 2.0 };
    return result;
}
NI double dr_fp_div(double x, double y) { return x / y; }
NI unsigned dr_effect(unsigned x) { dr_seen += x; return x + 3; }
NI unsigned dr_call(unsigned x) { return (dr_effect(x * 13u) * 17u) & 0u; }
NI unsigned dr_loop(unsigned x, unsigned n)
{
    while (n--) {
        (void)((x * 13u) & 0u);
        x += n;
    }
    return x;
}
#if defined(__x86_64__) || defined(__aarch64__)
/* Compile-only naked probe: vpp currently still emits a prologue for this
   valid asm-only body, so executing its raw return would skip frame restore.
   Keep that existing gap visible without making this DCE test execute it. */
__attribute__((naked, noinline)) unsigned dr_naked(void)
{
#if defined(__x86_64__)
    __asm__("movl $17, %eax; retq");
#else
    __asm__("mov w0, #17; ret");
#endif
}
/* This ordinary callable function does trigger the absorbing-result gate.
   Opaque asm must make the whole-function liveness adapter decline safely. */
NI unsigned dr_opaque(unsigned x)
{
#if defined(__x86_64__)
    __asm__ volatile("# dr_opaque_barrier" : : : "cc");
#else
    __asm__ volatile("// dr_opaque_barrier" : : : "cc");
#endif
    return (x * 13u) & 0u;
}
NI U dr_asm(U x)
{
#if defined(__x86_64__)
    __asm__ volatile("addq $7, %0" : "+r"(x) : : "cc", "memory");
#else
    __asm__ volatile("add %0, %0, #7" : "+r"(x) : : "memory");
#endif
    return x * 13;
}
#endif
int main(void)
{
    if (dr_mul(0xffffffffU) || dr_mul64(0xffffffffffffffffULL)) return 1;
    if (dr_zero_left(123) || dr_ones(123) != 0xffffffffU || dr_rem_one(123) || dr_zero_shift(123)) return 15;
    if (dr_diamond(123, 0) || dr_diamond(123, 1) || dr_chain(123, 456)) return 2;
    if (dr_returned(19) != 264 || dr_returned(0xffffffffU) != 4) return 3;
    if (dr_div(1001, 13) || dr_remainder(1001, 17)) return 4;
    volatile unsigned input = 37;
    if (dr_volatile(&input) || input != 37) return 5;
    unsigned atom = 11;
    if (dr_atomic(&atom) || atom != 18) return 6;
    if (dr_overflow(0xffffffffU, 1) != 1 || dr_overflow(19, 37)) return 7;
    if (dr_carry(0xffffffffffffffffULL, 1, 0) != 1 || dr_carry(0, 0, 1) != 1) return 8;
    if (dr_carry(0xffffffffffffffffULL, 0, 1) != 1 || dr_carry(19, 37, 1) != 57) return 9;
    if (dr_partial(0xfedcba9876543210ULL, 0x1234abcdU) != 0xfedc7798765406bbULL) return 10;
    dr_pair p = dr_pair_result(19, 37);
    if (p.low != 247 || p.high != 629) return 11;
    dr_fp_pair f = dr_fp_result(3.25, 9.0);
    if (f.first != 4.75 || f.second != 4.5 || dr_fp_div(9.0, 4.0) != 2.25) return 12;
    if (dr_call(3) || dr_seen != 39 || dr_loop(11, 4) != 17) return 13;
#if defined(__x86_64__) || defined(__aarch64__)
    if (dr_asm(11) != 234 || dr_opaque(11)) return 14;
#endif
    return 0;
}
