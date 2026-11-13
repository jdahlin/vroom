/* Literal-answer multiplication checks: native single-op recipes; wasm keeps mul. */
#define NI __attribute__((noinline))
NI unsigned mc_u32_3(unsigned x) { return x * 3; }
NI unsigned mc_u32_5(unsigned x) { return x * 5; }
NI unsigned mc_u32_9(unsigned x) { return x * 9; }
NI int mc_s32_3(int x) { return x * 3; }
NI int mc_s32_5(int x) { return x * 5; }
NI int mc_s32_9(int x) { return x * 9; }
NI unsigned long long mc_u64_3(unsigned long long x) { return x * 3; }
NI unsigned long long mc_u64_5(unsigned long long x) { return x * 5; }
NI unsigned long long mc_u64_9(unsigned long long x) { return x * 9; }
NI long long mc_s64_3(long long x) { return x * 3; }
NI long long mc_s64_5(long long x) { return x * 5; }
NI long long mc_s64_9(long long x) { return x * 9; }
NI unsigned mc_commuted(unsigned x) { return 5 * x; }
NI long long mc_sign_extend(int x) { return (long long)(x * 3); }
NI unsigned long long mc_zero_extend(unsigned x) { return (unsigned long long)(x * 3); }
NI unsigned long long mc_narrow(unsigned long long x) { return (unsigned long long)((unsigned)x * 3U); }
NI unsigned mc_compound(unsigned *p) { return *p *= 9; }
NI unsigned mc_post(unsigned *p) { return (*p)++ * 5; }
static unsigned calls, cell;
NI static unsigned *mc_pointer(void) { calls++; return &cell; }
NI unsigned mc_pointer3(void) { return *mc_pointer() *= 3; }
NI static unsigned mc_next(void) { calls++; return 17; }
NI unsigned mc_call5(void) { return mc_next() * 5; }
volatile unsigned mc_observed;
NI unsigned mc_volatile9(void) { return mc_observed * 9; }
int main(void) {
    if (mc_u32_3(0U) != 0U) return 1;
    if (mc_u32_5(0U) != 0U) return 1;
    if (mc_u32_9(0U) != 0U) return 1;
    if (mc_u32_3(1U) != 3U) return 1;
    if (mc_u32_5(1U) != 5U) return 1;
    if (mc_u32_9(1U) != 9U) return 1;
    if (mc_u32_3(4294967295U) != 4294967293U) return 1;
    if (mc_u32_5(4294967295U) != 4294967291U) return 1;
    if (mc_u32_9(4294967295U) != 4294967287U) return 1;
    if (mc_u32_3(2147483648U) != 2147483648U) return 1;
    if (mc_u32_5(2147483648U) != 2147483648U) return 1;
    if (mc_u32_9(2147483648U) != 2147483648U) return 1;
    if (mc_u32_3(305419896U) != 916259688U) return 1;
    if (mc_u32_5(305419896U) != 1527099480U) return 1;
    if (mc_u32_9(305419896U) != 2748779064U) return 1;
    if (mc_s32_3(0) != 0) return 1;
    if (mc_s32_5(0) != 0) return 1;
    if (mc_s32_9(0) != 0) return 1;
    if (mc_s32_3(1) != 3) return 1;
    if (mc_s32_5(1) != 5) return 1;
    if (mc_s32_9(1) != 9) return 1;
    if (mc_s32_3(-1) != -3) return 1;
    if (mc_s32_5(-1) != -5) return 1;
    if (mc_s32_9(-1) != -9) return 1;
    if (mc_s32_3(238609294) != 715827882) return 1;
    if (mc_s32_5(238609294) != 1193046470) return 1;
    if (mc_s32_9(238609294) != 2147483646) return 1;
    if (mc_s32_3(-238609294) != -715827882) return 1;
    if (mc_s32_5(-238609294) != -1193046470) return 1;
    if (mc_s32_9(-238609294) != -2147483646) return 1;
    if (mc_u64_3(0ULL) != 0ULL) return 1;
    if (mc_u64_5(0ULL) != 0ULL) return 1;
    if (mc_u64_9(0ULL) != 0ULL) return 1;
    if (mc_u64_3(1ULL) != 3ULL) return 1;
    if (mc_u64_5(1ULL) != 5ULL) return 1;
    if (mc_u64_9(1ULL) != 9ULL) return 1;
    if (mc_u64_3(18446744073709551615ULL) != 18446744073709551613ULL) return 1;
    if (mc_u64_5(18446744073709551615ULL) != 18446744073709551611ULL) return 1;
    if (mc_u64_9(18446744073709551615ULL) != 18446744073709551607ULL) return 1;
    if (mc_u64_3(9223372036854775808ULL) != 9223372036854775808ULL) return 1;
    if (mc_u64_5(9223372036854775808ULL) != 9223372036854775808ULL) return 1;
    if (mc_u64_9(9223372036854775808ULL) != 9223372036854775808ULL) return 1;
    if (mc_u64_3(1311768467463790320ULL) != 3935305402391370960ULL) return 1;
    if (mc_u64_5(1311768467463790320ULL) != 6558842337318951600ULL) return 1;
    if (mc_u64_9(1311768467463790320ULL) != 11805916207174112880ULL) return 1;
    if (mc_s64_3(0LL) != 0LL) return 1;
    if (mc_s64_5(0LL) != 0LL) return 1;
    if (mc_s64_9(0LL) != 0LL) return 1;
    if (mc_s64_3(1LL) != 3LL) return 1;
    if (mc_s64_5(1LL) != 5LL) return 1;
    if (mc_s64_9(1LL) != 9LL) return 1;
    if (mc_s64_3(-1LL) != -3LL) return 1;
    if (mc_s64_5(-1LL) != -5LL) return 1;
    if (mc_s64_9(-1LL) != -9LL) return 1;
    if (mc_s64_3(1024819115206086200LL) != 3074457345618258600LL) return 1;
    if (mc_s64_5(1024819115206086200LL) != 5124095576030431000LL) return 1;
    if (mc_s64_9(1024819115206086200LL) != 9223372036854775800LL) return 1;
    if (mc_s64_3(-1024819115206086200LL) != -3074457345618258600LL) return 1;
    if (mc_s64_5(-1024819115206086200LL) != -5124095576030431000LL) return 1;
    if (mc_s64_9(-1024819115206086200LL) != -9223372036854775800LL) return 1;
    if (mc_narrow(0xdeadbeefffffffffULL) != 0xfffffffdULL) return 11;
    if (mc_commuted(0xffffffffU) != 0xfffffffbU) return 2;
    if (mc_sign_extend(-715827882) != -2147483646LL) return 3;
    if (mc_zero_extend(0xffffffffU) != 0xfffffffdULL) return 4;
    cell = 0xffffffffU; if (mc_compound(&cell) != 0xfffffff7U || cell != 0xfffffff7U) return 5;
    cell = 0xffffffffU; if (mc_post(&cell) != 0xfffffffbU || cell != 0) return 6;
    calls = 0; cell = 7; if (mc_pointer3() != 21 || calls != 1 || cell != 21) return 7;
    calls = 0; if (mc_call5() != 85 || calls != 1) return 8;
    mc_observed = 13; if (mc_volatile9() != 117 || mc_observed != 13) return 9;
    return 0;
}
