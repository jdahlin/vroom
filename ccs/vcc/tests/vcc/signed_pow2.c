/* Signed power-of-two division rounds toward zero, including negative divisors. */
#define NI __attribute__((noinline))
NI int sp_div32_8(int n) { return n / 8; }
NI int sp_rem32_8(int n) { return n % 8; }
NI int sp_div32_neg8(int n) { return n / -8; }
NI int sp_rem32_neg8(int n) { return n % -8; }
NI long long sp_div64_16(long long n) { return n / 16LL; }
NI long long sp_rem64_16(long long n) { return n % 16LL; }
NI long long sp_div64_neg16(long long n) { return n / -16LL; }
NI long long sp_rem64_neg16(long long n) { return n % -16LL; }
NI int sp_div32_2(int n) { return n / 2; }
NI int sp_rem32_2(int n) { return n % -2; }
NI int sp_div32_high(int n) { return n / 1073741824; }
NI int sp_rem32_high(int n) { return n % -1073741824; }
NI long long sp_div64_2(long long n) { return n / -2LL; }
NI long long sp_rem64_2(long long n) { return n % 2LL; }
NI long long sp_div64_high(long long n) { return n / 4611686018427387904LL; }
NI long long sp_rem64_high(long long n) { return n % -4611686018427387904LL; }
NI int sp_div32_min(int n) { return n / (-2147483647 - 1); }
NI int sp_rem32_min(int n) { return n % (-2147483647 - 1); }
NI long long sp_div64_min(long long n) { return n / (-9223372036854775807LL - 1); }
NI long long sp_rem64_min(long long n) { return n % (-9223372036854775807LL - 1); }
NI int sp_div32_neg1(int n) { return n / -1; }
NI int sp_rem32_neg1(int n) { return n % -1; }
NI long long sp_div64_neg1(long long n) { return n / -1LL; }
NI long long sp_rem64_neg1(long long n) { return n % -1LL; }
NI int sp_nested(int a, int b, int c) { return (a / 8 + b % -8) * (c / -2); }
NI long long sp_narrow(long long n) { return (int)n / 8; }
static volatile int sp_calls, sp_input;
NI int sp_next(void) { ++sp_calls; return -19; }
NI int sp_effect_div(void) { return sp_next() / 8; }
NI int sp_effect_rem(void) { return sp_next() % -8; }
NI int sp_volatile_div(void) { return sp_input / -8; }
NI int sp_compound(volatile int *p) { return *p %= -8; }
NI int sp_rhs_effect(int n) { return n / (sp_calls++, -8); }
NI int sp_char(signed char n) { n /= -8; return n; }
NI int sp_short(short n) { n %= 8; return n; }
int main(void)
{
	static const int a[] = { -2147483647-1, -2147483647, -1025, -17, -16,
		-15, -9, -8, -7, -1, 0, 1, 7, 8, 9, 15, 16, 17, 2147483647 };
	static const long long b[] = { -9223372036854775807LL-1, -9223372036854775807LL,
		-4294967297LL, -33, -32, -31, -17, -16, -15, -1, 0, 1, 15, 16, 17,
		31, 32, 33, 4294967297LL, 9223372036854775807LL };
	unsigned i;
	volatile int eight = 8, two32 = 2, high32 = 1073741824;
	volatile long long sixteen = 16, two64 = 2, high64 = 4611686018427387904LL;
	for (i = 0; i < sizeof(a)/sizeof(a[0]); ++i) {
		int n = a[i], q = n / eight, r = n % eight;
		if (sp_div32_2(n) != n / two32 || sp_rem32_2(n) != n % two32) return 17;
		if (sp_div32_high(n) != n / high32 || sp_rem32_high(n) != n % high32) return 18;
		if (sp_div32_8(n) != q || sp_rem32_8(n) != r) return 1;
		if (sp_div32_neg8(n) != -q || sp_rem32_neg8(n) != r) return 2;
		if (sp_div32_min(n) != (n == (-2147483647-1))) return 3;
		if (sp_rem32_min(n) != (n == (-2147483647-1) ? 0 : n)) return 4;
		if (n != (-2147483647-1) && (sp_div32_neg1(n) != -n || sp_rem32_neg1(n))) return 5;
	}
	for (i = 0; i < sizeof(b)/sizeof(b[0]); ++i) {
		long long n = b[i], q = n / sixteen, r = n % sixteen;
		if (n != (-9223372036854775807LL-1) && (sp_div64_neg1(n) != -n || sp_rem64_neg1(n))) return 21;
		if (sp_div64_2(n) != -(n / two64) || sp_rem64_2(n) != n % two64) return 19;
		if (sp_div64_high(n) != n / high64 || sp_rem64_high(n) != n % high64) return 20;
		if (sp_div64_16(n) != q || sp_rem64_16(n) != r) return 6;
		if (sp_div64_neg16(n) != -q || sp_rem64_neg16(n) != r) return 7;
		if (sp_div64_min(n) != (n == (-9223372036854775807LL-1))) return 8;
		if (sp_rem64_min(n) != (n == (-9223372036854775807LL-1) ? 0 : n)) return 9;
	}
	if (sp_narrow(0xffffffff00000009LL) != 1 || sp_narrow(0xffffffffLL) != 0) return 10;
	if (sp_narrow(0x12345678ffffffedLL) != -2 || sp_nested(-19, -23, 7) != 27) return 22;
	sp_calls = 0;
	if (sp_effect_div() != -2 || sp_calls != 1) return 11;
	if (sp_effect_rem() != -3 || sp_calls != 2) return 12;
	if (sp_rhs_effect(-19) != 2 || sp_calls != 3) return 13;
	sp_input = -19;
	if (sp_volatile_div() != 2 || sp_input != -19) return 14;
	if (sp_compound(&sp_input) != -3 || sp_input != -3) return 15;
	if (sp_char(-127) != 15 || sp_short(-32767) != -7) return 16;
	return 0;
}
