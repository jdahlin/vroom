/* Machine recipes preserve signed products, low-word wrap and evaluation. */
#define NI __attribute__((noinline))
NI unsigned br_mul32(unsigned x) { return x * 8U; }
NI int br_signed32(int x) { return x * 8; }
NI unsigned long long br_mul64(unsigned long long x) { return x * 64ULL; }
NI long long br_signed64(long long x) { return x * 64LL; }
NI unsigned br_high32(unsigned x) { return x * 0x80000000U; }
NI unsigned long long br_high64(unsigned long long x) { return x * 0x8000000000000000ULL; }
NI unsigned br_left(unsigned x) { return 8U * x; }
NI unsigned br_not32(unsigned x) { return x ^ ~0U; }
NI unsigned long long br_not64(unsigned long long x) { return x ^ ~0ULL; }
NI int br_not_signed(int x) { return x ^ -1; }
NI unsigned br_not_left(unsigned x) { return ~0U ^ x; }
NI unsigned long long br_narrow(unsigned long long x) { return (unsigned)x * 8U; }
NI long long br_extend(int x) { return (long long)(x * 8); }
NI unsigned br_compound(unsigned *p) { return *p *= 8; }
NI unsigned br_xor_compound(unsigned *p) { return *p ^= ~0U; }
static unsigned br_calls, br_cell;
NI static unsigned br_next(void) { br_calls++; return 13; }
NI static unsigned *br_pointer(void) { br_calls++; return &br_cell; }
NI unsigned br_call(void) { return br_next() * 8; }
NI unsigned br_rhs(void) { return br_cell * (br_next(), 8U); }
NI unsigned br_pointer_mul(void) { return *br_pointer() *= 8; }
NI unsigned br_pointer_xor(void) { return *br_pointer() ^= ~0U; }
volatile unsigned br_observed;
NI unsigned br_volatile(void) { return br_observed * 8; }
NI unsigned br_volatile_xor(void) { return br_observed ^ ~0U; }
NI unsigned br_dynamic(unsigned x, unsigned y) { return x * y; }
#ifndef __wasm__
NI int br_overflow(unsigned x, unsigned *p) { return __builtin_mul_overflow(x, 8U, p); }
NI int br_signed_overflow(int x, int *p) { return __builtin_mul_overflow(x, 8, p); }
#endif
int main(void) {
 unsigned u; int s;
 if (br_mul32(0xffffffffU) != 0xfffffff8U || br_mul32(0x80000000U) != 0) return 1;
 if (br_signed32(-268435456) != (-2147483647-1) || br_signed32(268435455) != 2147483640) return 2;
 if (br_mul64(~0ULL) != 0xffffffffffffffc0ULL || br_mul64(0x8000000000000000ULL) != 0) return 3;
 if (br_signed64(-144115188075855872LL) != (-9223372036854775807LL-1) || br_signed64(144115188075855871LL) != 9223372036854775744LL) return 4;
 if (br_high32(3) != 0x80000000U || br_high32(2) != 0 || br_high64(3) != 0x8000000000000000ULL || br_high64(2) != 0) return 5;
 if (br_left(0xffffffffU) != 0xfffffff8U || br_not32(0xdeadbeefU) != 0x21524110U || br_not64(0x123456789abcdef0ULL) != 0xedcba9876543210fULL) return 6;
 if (br_not_signed(-2147483647-1) != 2147483647 || br_not_left(0xdeadbeefU) != 0x21524110U) return 7;
 if (br_narrow(0xdeadbeefffffffffULL) != 0xfffffff8ULL || br_extend(-1) != -8LL) return 8;
 u=0xffffffffU; if (br_compound(&u) != 0xfffffff8U || u != 0xfffffff8U) return 9;
 if (br_xor_compound(&u) != 7 || u != 7) return 10;
 br_calls=0; if (br_call()!=104 || br_calls!=1) return 11;
 br_calls=0; br_cell=7; if (br_rhs()!=56 || br_calls!=1) return 12;
 br_calls=0; if (br_pointer_mul()!=56 || br_calls!=1 || br_cell!=56) return 13;
 br_calls=0; if (br_pointer_xor()!=0xffffffc7U || br_calls!=1 || br_cell!=0xffffffc7U) return 14;
 br_observed=13; if (br_volatile()!=104 || br_volatile_xor()!=0xfffffff2U || br_observed!=13) return 15;
 if (br_dynamic(7,11)!=77) return 16;
#ifndef __wasm__
 if (!br_overflow(0xffffffffU,&u) || u!=0xfffffff8U || br_overflow(7,&u) || u!=56) return 17;
 if (!br_signed_overflow(268435456,&s) || s!=(-2147483647-1) || br_signed_overflow(-268435456,&s) || s!=(-2147483647-1)) return 18;
#endif
 return 0;
}
