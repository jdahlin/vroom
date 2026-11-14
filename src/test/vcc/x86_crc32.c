/* SSE4.2 crc32 builtins (clang's crc32intrin.h) against a bitwise CRC-32C;
   the accumulator and data each evaluated once, the 64-bit form's high
   accumulator bits ignored as the instruction does. */
#if defined(__x86_64__)
#include <nmmintrin.h>
static int evaluations;
__attribute__((noinline)) static unsigned long long arg(unsigned long long x) { ++evaluations; return x; }
static unsigned ref(unsigned crc, unsigned long long v, int bytes)
{
	for (int i = 0; i < bytes; i++) {
		crc ^= (unsigned char)(v >> (8 * i));
		for (int k = 0; k < 8; k++) crc = crc >> 1 ^ (0x82f63b78u & -(crc & 1));
	}
	return crc;
}
__attribute__((target("sse4.2"),noinline))
static int check(void)
{
	unsigned long long x = 0x9e3779b97f4a7c15ull;
	for (int i = 0; i < 1000; i++) {
		unsigned c = (unsigned)(x >> 17);
		x = x * 6364136223846793005ull + 1442695040888963407ull;
		if (__builtin_ia32_crc32qi((unsigned)arg(c), (unsigned char)arg(x)) != ref(c, x, 1)) return 1;
		if (__builtin_ia32_crc32hi((unsigned)arg(c), (unsigned short)arg(x)) != ref(c, x, 2)) return 2;
		if (__builtin_ia32_crc32si((unsigned)arg(c), (unsigned)arg(x)) != ref(c, x, 4)) return 3;
		if (__builtin_ia32_crc32di(arg(c | x << 32), arg(x)) != ref(c, x, 8)) return 4;
		if (_mm_crc32_u8(c, (unsigned char)x) != ref(c, x, 1)) return 5;
		if (_mm_crc32_u64(c, x) != ref(c, x, 8)) return 6;
	}
	return evaluations == 8000 ? 0 : 7;
}
int main(void)
{
	unsigned ax, bx, cx, dx;
	__asm__("cpuid" : "=a"(ax), "=b"(bx), "=c"(cx), "=d"(dx) : "0"(1), "2"(0));
	return cx & (1u << 20) ? check() : 0;
}
#else
int main(void) { return 0; }
#endif
