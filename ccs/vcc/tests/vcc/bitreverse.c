/* __builtin_bitreverse8..64 (the wasm generator has no instruction for them) */
#include <stdio.h>

static unsigned long long ref(unsigned long long v, int bits)
{
	unsigned long long r = 0;

	for (int i = 0; i < bits; i++)
		r |= (v >> i & 1) << (bits - 1 - i);
	return r;
}

int main(void)
{
	unsigned long long x = 0x0123456789abcdefull;
	int bad = 0;

	for (int i = 0; i < 64; i++, x = x * 6364136223846793005ull + 1442695040888963407ull) {
		volatile unsigned long long v = x;
		bad |= __builtin_bitreverse8((unsigned char)v) != ref(v & 0xff, 8);
		bad |= __builtin_bitreverse16((unsigned short)v) != ref(v & 0xffff, 16);
		bad |= __builtin_bitreverse32((unsigned)v) != ref(v & 0xffffffff, 32);
		bad |= __builtin_bitreverse64(v) != ref(v, 64);
	}
	printf("%x %x %x %llx %d\n", __builtin_bitreverse8(0x13), __builtin_bitreverse16(0x1234),
	       __builtin_bitreverse32(0x12345678), __builtin_bitreverse64(0x123456789abcdef0ull), bad);
	return bad;
}
