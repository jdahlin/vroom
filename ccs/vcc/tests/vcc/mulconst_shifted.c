/* x * (3, 5 or 9 << n) as an add of a shifted x and a shift, against the multiply by a run-time constant. */
#include <stdint.h>

#define T(n, k) \
	__attribute__((noinline)) int64_t m64_##n(int64_t x) { return x * (k); } \
	__attribute__((noinline)) uint32_t m32_##n(uint32_t x) { return x * (uint32_t)(k); } \
	__attribute__((noinline)) int m32s_##n(int x) { return (int)((unsigned)x * (unsigned)(k)); }

T(6, 6) T(10, 10) T(12, 12) T(18, 18) T(20, 20) T(24, 24) T(36, 36) T(40, 40) T(72, 72) T(96, 96)
T(big, 9LL << 29) T(huge, 3LL << 60)

static int fails;

#define CHECK(n, k) do { \
	volatile int64_t kv = (k); \
	for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) { \
		int64_t x = vals[i]; \
		if (m64_##n(x) != (int64_t)((uint64_t)x * (uint64_t)kv)) fails++; \
		if (m32_##n((uint32_t)x) != (uint32_t)((uint32_t)x * (uint32_t)kv)) fails++; \
		if (m32s_##n((int)x) != (int)((uint32_t)x * (uint32_t)kv)) fails++; \
	} } while (0)

int main(void)
{
	static const int64_t vals[] = { 0, 1, -1, 7, -7, 12345, -98765, 0x7fffffff, 0x80000000LL, 0xffffffffLL,
					0x123456789abcdefLL, INT64_MIN, INT64_MAX };

	CHECK(6, 6); CHECK(10, 10); CHECK(12, 12); CHECK(18, 18); CHECK(20, 20); CHECK(24, 24);
	CHECK(36, 36); CHECK(40, 40); CHECK(72, 72); CHECK(96, 96);
	CHECK(big, 9LL << 29); CHECK(huge, 3LL << 60);
	return fails != 0;
}
