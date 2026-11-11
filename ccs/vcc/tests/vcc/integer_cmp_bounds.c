#define NI __attribute__((noinline))
static unsigned calls;
static volatile unsigned observed;
NI unsigned next(void) { calls++; return observed; }
NI int cb_low(unsigned x) { return x < 0U; }
NI int cb_high(unsigned x) { return x <= 0xffffffffU; }
NI int cb_reverse(unsigned x) { return 0U > x; }
NI int cb_effect(void) { return next() < 0U; }
NI int cb_branch(void) { if (next() >= 0U) return 9; return 8; }
NI int cb_volatile(void) { return observed < 0U; }
NI int cb_smax(int x) { return x > 2147483647; }
NI int cb_u64(unsigned long long x) { return x >= 0ULL; }
NI int cb_atomic(_Atomic unsigned *p) { return __c11_atomic_load(p, 5) < 0U; }
enum { cb_enum_min = (-2147483647 - 1), cb_enum_zero = 0 };
/* Bounds written as ordinary C constant expressions, in either order. */
NI int cb_expr_enum(int x) { return x < cb_enum_min; }
NI int cb_expr_enum_zero(unsigned x) { return x < (unsigned)cb_enum_zero; }
NI int cb_expr_smin(int x) { return x < (-2147483647 - 1); }
NI int cb_expr_smin_reverse(int x) { return (-2147483647 - 1) <= x; }
NI int cb_expr_s64min(long long x) { return x >= (-9223372036854775807LL - 1LL); }
NI int cb_expr_umax(unsigned x) { return x > (unsigned)-1; }
NI int cb_expr_u64max(unsigned long long x) { return (unsigned long long)-1 >= x; }
NI int cb_expr_trunc(unsigned x) { return x < (unsigned short)65536U; }
NI int cb_expr_zero(unsigned x) { return x < (unsigned)(1 - 1); }
NI int cb_expr_complement(unsigned x) { return x > ~0U; }
NI int cb_expr_shift(unsigned x) { return x < ((1U << 4) - 16U); }
NI int cb_expr_div(unsigned x) { return x > (~0U / 1U); }
NI int cb_expr_effect(void) { return next() < (unsigned)(1 - 1); }
NI int cb_expr_effect_left(void) { return (unsigned)(1 - 1) > next(); }
NI int cb_expr_volatile(void) { return observed < (unsigned)(1 - 1); }
NI int cb_expr_atomic(_Atomic unsigned *p) { return __c11_atomic_load(p, 5) < (unsigned)(1 - 1); }
/* The constant-looking side has a call and must not be dropped. */
NI int cb_impure_rhs(unsigned x) { return x < (next(), 0U); }
NI int cb_impure_tree(unsigned x) { return x < (next() * 0U); }
NI int cb_impure_volatile(unsigned x) { return x < (observed - observed); }
NI int cb_wide_dynamic(long long x) { return x < (long long)(-2147483647 - 1); }
NI int cb_dynamic(unsigned x, unsigned y) { return x < y; }
NI int cb_mixed(int x) { return x < 0U; }
int main(void) {
 _Atomic unsigned atom = 11;
 observed = 7; calls = 0;
 if (cb_low(0) || cb_low(~0U) || !cb_high(0) || !cb_high(~0U) || cb_reverse(1)) return 1;
 if (cb_effect() || calls != 1 || cb_branch() != 9 || calls != 2 || cb_volatile()) return 2;
 if (cb_smax(-1) || cb_smax(2147483647) || !cb_u64(~0ULL) || cb_atomic(&atom)) return 4;
 if (cb_expr_enum(0) || cb_expr_enum_zero(7) || cb_expr_smin(-2147483647-1) || cb_expr_smin(2147483647) ||
     !cb_expr_smin_reverse(-2147483647-1) || !cb_expr_s64min(-9223372036854775807LL-1LL)) return 5;
 if (cb_expr_umax(~0U) || !cb_expr_u64max(~0ULL) || cb_expr_trunc(9) || cb_expr_zero(7) || cb_expr_complement(3) || cb_expr_shift(1) || cb_expr_div(3)) return 6;
 calls = 0;
 if (cb_expr_effect() || cb_expr_effect_left() || calls != 2 || cb_expr_volatile() || cb_expr_atomic(&atom)) return 7;
 calls = 0;
 if (cb_impure_rhs(4) || cb_impure_tree(4) || calls != 2 || cb_impure_volatile(4) || !cb_wide_dynamic(-2147483649LL) || cb_wide_dynamic(0)) return 8;
 if (!cb_dynamic(1,2) || cb_dynamic(2,1) || cb_mixed(-1)) return 3;
 return 0;
}
