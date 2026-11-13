/* A constant the lowering builds in x17 next to its reader (a multiplier, VIR at
   -O2) must be read before another record rebuilds x17: an add of an immediate that
   does not encode, a load or store at a large frame offset.  The record fold moved
   the multiply past such a record (madd of the product and the sum) and multiplied
   by the immediate instead.  Edge cases: both operand orders, sub, 32 bits, a
   multiplier the fold turns into shifts (no x17) alongside one it does not. */
#define NI __attribute__((noinline))

NI long h(long a, long b) { long m = a * 7; long t = b + 0x12345678; return m + t; }
NI long h2(long a, long b) { long t = b + 0x12345678; long m = a * 7; return t + m; }
NI long h3(long a, long b) { long m = a * 11; long t = b - 0x7654321; return t - m; }
NI int h4(int a, int b) { int m = a * 13; int t = b + 0x1234567; return m + t; }
NI long h5(long a, long b) { long m = a * 100; long t = b ^ 0x123456789abL; return m + t; }
NI long far(long a)
{
	volatile long big[6000];

	big[5999] = a;
	long m = a * 7;
	long t = big[5999];
	return m + t;
}

int main(void)
{
	if (h(3, 4) != 21 + 4 + 0x12345678) return 1;
	if (h2(3, 4) != 21 + 4 + 0x12345678) return 2;
	if (h3(3, 4) != 4 - 0x7654321 - 33) return 3;
	if (h4(3, 4) != 39 + 4 + 0x1234567) return 4;
	if (h5(3, 4) != 300 + (4 ^ 0x123456789abL)) return 5;
	if (far(5) != 40) return 6;
	return 0;
}
