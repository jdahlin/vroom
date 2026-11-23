#if defined(__aarch64__)
typedef union { __fp16 half; unsigned short bits; } half_bits;

__attribute__((target("arch=armv8.2-a+fp16"), noinline))
static int check(void)
{
    volatile half_bits one = {.bits = 0x3c00}, tiny = {.bits = 0x1000};
    volatile half_bits negzero = {.bits = 0x8000}, subnormal = {.bits = 1};
    half_bits rounded = {.half = __builtin_neon_vaddh_f16(one.half, tiny.half)};
    half_bits difference = {.half = __builtin_neon_vsubh_f16(one.half, tiny.half)};
    half_bits zero = {.half = __builtin_neon_vaddh_f16(negzero.half, negzero.half)};
    half_bits doubled = {.half = __builtin_neon_vsubh_f16(subnormal.half,
                                                        -subnormal.half)};
    if (rounded.bits != 0x3c00 || difference.bits != 0x3bff)
        return 1;
    if (zero.bits != 0x8000 || doubled.bits != 2)
        return 2;
    return 0;
}

int main(void) { return check(); }
#else
int main(void) { return 0; }
#endif
