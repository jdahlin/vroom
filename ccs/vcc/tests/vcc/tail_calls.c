/* Direct sibling calls must preserve argument cycles and the caller's ABI.
   Deliberately keep each positive wrapper's body to one return statement. */
#define NI __attribute__((noinline))
typedef long long I;
typedef unsigned long long U;
static volatile int tc_cleaned;
static volatile int tc_effects;
NI int tc_sum32(int a, int b) { return a * 3 + b; }
NI unsigned tc_unsigned(unsigned a) { return a + 17; }
NI I tc_pack64(I a, I b, I c) { return a * 10000 + b * 100 + c; }
NI I tc_pair64(I a, I b) { return a ^ b; }
NI U tc_pairu64(U a, U b) { return a ^ b; }
NI int *tc_pointer(int *p) { return p + 1; }
NI int *tc_pointer_pair(int *p, int *q) { return *p > *q ? p : q; }
NI int tc_pack32(int a, int b, int c) { return a * 10000 + b * 100 + c; }
NI int tc_six(int a, int b, int c, int d, int e, int f)
{
    return a + b * 3 + c * 7 + d * 11 + e * 17 + f * 23;
}
NI int tc_zero(void) { return 71; }
NI int tc_eight(int a, int b, int c, int d, int e, int f, int g, int h)
{
    return tc_six(a, b, c, d, e, f) + g * 31 + h * 43;
}
NI int tc_nine(int a, int b, int c, int d, int e, int f, int g, int h, int i)
{
    return tc_eight(a, b, c, d, e, f, g, h) + i * 47;
}
NI int tc_i32(int a, int b) { return tc_sum32(b, a); }
NI unsigned tc_u32(unsigned a) { return tc_unsigned(a); }
NI I tc_i64(I a, I b, I c) { return tc_pack64(c, a, b); }
NI I tc_literal64(I a) { return tc_pair64(a, 0x123456789abcdefLL); }
NI U tc_uliteral64(U a) { return tc_pairu64(a, 0xfedcba9876543210ULL); }
NI int *tc_ptr(int *p) { return tc_pointer(p); }
NI int *tc_ptr_swap(int *p, int *q) { return tc_pointer_pair(q, p); }
NI int tc_constants(int a) { return tc_pack32(a, 17, -9); }
NI int tc_cycle6(int a, int b, int c, int d, int e, int f)
{
    return tc_six(c, a, f, b, d, e);
}
NI int tc_duplicate(int a, int b) { return tc_six(a, a, b, b, a, b); }
NI int tc_empty(void) { return tc_zero(); }
/* These fit the arm64 register ABI, but need stack arguments on x86. */
NI int tc_cycle8(int a, int b, int c, int d, int e, int f, int g, int h)
{
    return tc_eight(h, g, f, e, d, c, b, a);
}

/* Negative controls retain normal calls in this initial conservative slice. */
NI I tc_conversion(int a) { return tc_sum32(a, 1); }
NI int tc_arithmetic(int a) { return tc_sum32(a + 1, 17); }
NI int tc_effect_argument(int a) { return tc_sum32((++tc_effects, a), 17); }
NI int tc_cast(I a) { return tc_sum32((int)a, 17); }
NI int tc_local(int a) { int value = a + 2; return tc_sum32(value, 3); }
NI int tc_load(const int *p) { return *p; }
NI int tc_address(int a) { return tc_load(&a); }
NI void tc_clean(int *p) { tc_cleaned += *p; }
NI int tc_cleanup(int a)
{
    int value __attribute__((cleanup(tc_clean))) = a;
    return tc_sum32(a, 3);
}
NI int tc_vla(int n)
{
    volatile int values[n];
    values[n - 1] = n;
    return tc_sum32(values[n - 1], 3);
}
NI int tc_stack9(int a)
{
    return tc_nine(a, 2, 3, 4, 5, 6, 7, 8, 9);
}
NI int tc_vararg_target(int a, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, a);
    int b = __builtin_va_arg(ap, int);
    int c = __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return a + b * 3 + c * 7;
}
NI int tc_vararg(int a) { return tc_vararg_target(a, 3, 7); }
NI int tc_indirect(int (*fn)(int, int), int a) { return fn(a, 17); }
NI double tc_fp_target(double a) { return a + 0.5; }
NI double tc_fp(double a) { return tc_fp_target(a); }
NI signed char tc_byte_target(signed char a) { return a - 3; }
NI signed char tc_narrow(signed char a) { return tc_byte_target(a); }
__attribute__((noinline, optnone)) int tc_noopt(int a) { return tc_sum32(a, 17); }
/* Attributes on earlier declarations must survive a later plain definition. */
__attribute__((disable_tail_calls)) int tc_disabled(int a);
NI int tc_disabled(int a) { return tc_sum32(a, 17); }
__attribute__((not_tail_called)) int tc_notail_target(int a);
NI int tc_notail_target(int a) { return a + 13; }
NI int tc_notail(int a) { return tc_notail_target(a); }
__attribute__((returns_twice)) int tc_twice_target(int a);
NI int tc_twice_target(int a) { return a + 7; }
NI int tc_twice(int a) { return tc_twice_target(a); }
__attribute__((returns_twice)) int tc_caller_twice(int a);
NI int tc_caller_twice(int a) { return tc_sum32(a, 17); }
int main(void)
{
    if (tc_i32(-11, 7) != 10 || tc_u32(0xfffffff8U) != 9) return 1;
    if (tc_i64(3, 5, 7) != 70305) return 2;
    if (tc_literal64(0x100000001LL) != 0x123456689abcdeeLL) return 3;
    if (tc_uliteral64(0x100000001ULL) != 0xfedcba9976543211ULL) return 4;
    int values[3] = { 11, 29, 7 };
    if (tc_ptr(values) != values + 1 || tc_ptr_swap(values, values + 1) != values + 1) return 5;
    if (tc_constants(3) != 31691 || tc_cycle6(1, 2, 3, 4, 5, 6) != 253) return 6;
    if (tc_duplicate(3, 7) != 350 || tc_empty() != 71) return 7;
    if (tc_cycle8(1, 2, 3, 4, 5, 6, 7, 8) != 368) return 8;
    if (tc_conversion(-11) != -32 || tc_arithmetic(3) != 29 || tc_cast(0x100000003LL) != 26) return 9;
    if (tc_local(3) != 18 || tc_address(11) != 11) return 10;
    if (tc_cleanup(7) != 24 || tc_cleaned != 7 || tc_vla(5) != 18) return 11;
    if (tc_stack9(1) != 1279 || tc_vararg(11) != 69 || tc_indirect(tc_sum32, 3) != 26) return 12;
    if (tc_fp(3.25) != 3.75 || tc_narrow(-19) != -22 || tc_noopt(3) != 26) return 13;
    if (tc_disabled(3) != 26 || tc_notail(3) != 16 || tc_twice(3) != 10 || tc_caller_twice(3) != 26) return 14;
    if (tc_effect_argument(3) != 26 || tc_effects != 1) return 15;
    return 0;
}
