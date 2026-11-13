/* Postfix update reads a volatile scalar once and returns the original value. */
#define NI __attribute__((noinline))
typedef unsigned long long U;
static volatile unsigned pe_global;
static volatile U pe_global64;
static volatile unsigned char pe_byte;
static volatile _Bool pe_bool;
static volatile float pe_float;
static volatile double pe_double;
static int *volatile pe_pointer;
static unsigned pe_calls;
NI unsigned pe_global_post(void) { return pe_global++; }
NI unsigned pe_global_pre(void) { return ++pe_global; }
NI unsigned pe_global_postdec(void) { return pe_global--; }
NI unsigned pe_global_predec(void) { return --pe_global; }
NI void pe_global_unused(void) { pe_global++; }
NI unsigned pe_global_cast(void) { return (unsigned char)pe_global++; }
NI U pe_global_post64(void) { return pe_global64++; }
NI unsigned pe_byte_post(void) { return pe_byte++; }
NI unsigned pe_byte_pre(void) { return ++pe_byte; }
NI int pe_bool_post(void) { return pe_bool++; }
NI int pe_bool_pre(void) { return ++pe_bool; }
NI float pe_float_post(void) { return pe_float++; }
NI double pe_double_post(void) { return pe_double++; }
NI int *pe_pointer_post(void) { return pe_pointer++; }
NI unsigned pe_post(volatile unsigned *p) { return (*p)++; }
NI unsigned pe_pre(volatile unsigned *p) { return ++*p; }
NI unsigned pe_postdec(volatile unsigned *p) { return (*p)--; }
NI unsigned pe_predec(volatile unsigned *p) { return --*p; }
NI void pe_unused(volatile unsigned *p) { (*p)++; }
NI short pe_short_post(volatile short *p) { return (*p)++; }
NI signed char pe_char_post(volatile signed char *p) { return (*p)--; }
NI unsigned pe_plain(unsigned *p) { return (*p)++; }
NI volatile unsigned *pe_address(void) { ++pe_calls; return &pe_global; }
NI unsigned pe_address_post(void) { return (*pe_address())++; }
struct pe_bits { unsigned keep:3, field:5, rest:24; };
NI unsigned pe_bits_post(volatile struct pe_bits *p) { return p->field++; }
NI void pe_bits_unused(volatile struct pe_bits *p) { p->field++; }
/* Atomics use their separate RMW protocol; do not impose a one-machine-read
   assertion on its compare-and-swap retry loop. */
NI unsigned pe_atomic_post(volatile _Atomic unsigned *p) { return (*p)++; }
NI unsigned pe_atomic_pre(volatile _Atomic unsigned *p) { return --*p; }
/* Address-backed values retain their existing snapshot path. */
NI U pe_wide_post(volatile unsigned __int128 *p) { return (U)((*p)++); }
/* Nested expressions keep other temporaries live while the old value is
   preserved and a working register is allocated. */
NI U pe_pressure(volatile U *p, U a, U b, U c, U d, U e, U f)
{
    return a + b * (c + d * (e + f * ((*p)++)));
}
NI double pe_fp_pressure(volatile double *p, double a, double b, double c,
                         double d, double e, double f)
{
    return a + b * (c + d * (e + f * ((*p)++)));
}
#if defined(__x86_64__) || defined(__aarch64__)
NI U pe_fixed_address(volatile U *p)
{
#if defined(__x86_64__)
    register volatile U *address __asm__("rdx") = p;
#else
    register volatile U *address __asm__("x2") = p;
#endif
    __asm__ volatile("" : "+r"(address));
    return (*address)++;
}
#endif
int main(void)
{
    pe_global = 11;
    if (pe_global_post() != 11 || pe_global != 12) return 1;
    if (pe_global_pre() != 13 || pe_global_postdec() != 13 || pe_global_predec() != 11) return 2;
    pe_global_unused();
    if (pe_global != 12) return 3;
    pe_global = 0x1234ffU;
    if (pe_global_cast() != 255 || pe_global != 0x123500U) return 4;
    pe_global64 = 0xffffffffffffffffULL;
    if (pe_global_post64() != 0xffffffffffffffffULL || pe_global64) return 5;
    pe_byte = 255;
    if (pe_byte_post() != 255 || pe_byte != 0) return 6;
    pe_byte = 255;
    if (pe_byte_pre() != 0 || pe_byte != 0) return 7;
    pe_bool = 0;
    if (pe_bool_post() != 0 || !pe_bool || pe_bool_pre() != 1 || !pe_bool) return 8;
    pe_float = 3.25f;
    pe_double = -4.5;
    if (pe_float_post() != 3.25f || pe_float != 4.25f || pe_double_post() != -4.5 || pe_double != -3.5) return 9;
    int array[3];
    pe_pointer = array;
    if (pe_pointer_post() != array || pe_pointer != array + 1) return 10;
    volatile unsigned value = 37;
    if (pe_post(&value) != 37 || value != 38 || pe_pre(&value) != 39) return 11;
    if (pe_postdec(&value) != 39 || value != 38 || pe_predec(&value) != 37) return 12;
    pe_unused(&value);
    if (value != 38) return 13;
    volatile short sh = -7;
    volatile signed char ch = -3;
    if (pe_short_post(&sh) != -7 || sh != -6 || pe_char_post(&ch) != -3 || ch != -4) return 14;
    unsigned ordinary = 73;
    if (pe_plain(&ordinary) != 73 || ordinary != 74) return 15;
    pe_global = 53;
    if (pe_address_post() != 53 || pe_global != 54 || pe_calls != 1) return 16;
    volatile struct pe_bits bits = { 5, 31, 0x123456 };
    if (pe_bits_post(&bits) != 31 || bits.field != 0 || bits.keep != 5 || bits.rest != 0x123456) return 17;
    pe_bits_unused(&bits);
    if (bits.field != 1 || bits.keep != 5 || bits.rest != 0x123456) return 18;
    volatile _Atomic unsigned atom = 7;
    if (pe_atomic_post(&atom) != 7 || atom != 8 || pe_atomic_pre(&atom) != 7 || atom != 7) return 19;
    volatile union { unsigned __int128 value; U words[2]; } wide;
    wide.words[0] = 0xffffffffffffffffULL;
    wide.words[1] = 3;
    if (pe_wide_post(&wide.value) != 0xffffffffffffffffULL || wide.words[0] || wide.words[1] != 4) return 20;
    volatile U pressure = 7;
    if (pe_pressure(&pressure, 1, 2, 3, 4, 5, 6) != 383 || pressure != 8) return 21;
    volatile double fp_pressure = 7.25;
    if (pe_fp_pressure(&fp_pressure, 1, 2, 3, 4, 5, 6) != 395 || fp_pressure != 8.25) return 22;
#if defined(__x86_64__) || defined(__aarch64__)
    if (pe_fixed_address(&pressure) != 8 || pressure != 9) return 23;
#endif
    return 0;
}
