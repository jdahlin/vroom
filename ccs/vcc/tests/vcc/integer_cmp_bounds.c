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
NI int cb_dynamic(unsigned x, unsigned y) { return x < y; }
NI int cb_mixed(int x) { return x < 0U; }
int main(void) {
 _Atomic unsigned atom = 11;
 observed = 7; calls = 0;
 if (cb_low(0) || cb_low(~0U) || !cb_high(0) || !cb_high(~0U) || cb_reverse(1)) return 1;
 if (cb_effect() || calls != 1 || cb_branch() != 9 || calls != 2 || cb_volatile()) return 2;
 if (cb_smax(-1) || cb_smax(2147483647) || !cb_u64(~0ULL) || cb_atomic(&atom)) return 4;
 if (!cb_dynamic(1,2) || cb_dynamic(2,1) || cb_mixed(-1)) return 3;
 return 0;
}
