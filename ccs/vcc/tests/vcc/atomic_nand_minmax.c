/* __atomic_fetch_{nand,max,min}, _fetch forms, __sync_*nand* / *max* / *min*, __atomic_test_and_set/clear,
   __c11_atomic_compare_exchange_*, every width, signed and unsigned: printed hash as clang */
#include <stdio.h>
#include <stdint.h>
static uint64_t h;
static void H(int64_t v) { h = h * 1000003u + (uint64_t)v; }
#define T(ty, a, b) do { ty x = (a), v = (b); \
	H(__atomic_fetch_max(&x, v, 5)); H(x); x = (a); H(__atomic_fetch_min(&x, v, 5)); H(x); \
	x = (a); H(__atomic_max_fetch(&x, v, 5)); x = (a); H(__atomic_min_fetch(&x, v, 5)); \
	x = (a); H(__atomic_fetch_nand(&x, v, 5)); H(x); x = (a); H(__atomic_nand_fetch(&x, v, 2)); H(x); } while (0)
#define ALL(a, b) T(int8_t, a, b); T(uint8_t, a, b); T(int16_t, a, b); T(uint16_t, a, b); \
	T(int32_t, a, b); T(uint32_t, a, b); T(int64_t, a, b); T(uint64_t, a, b)
__attribute__((noinline)) void run(int64_t a, int64_t b) { ALL(a, b); }
int main(void)
{
	static const int64_t v[] = { 0, 1, -1, 5, -128, 127, 200, 0x7fff, -0x8000, 0x12345678, -0x7fffffffll, 0x123456789abcdefll };
	int iv = 3, e = 3;
	unsigned uv = 7;
	char flag = 0;
	_Atomic int ai = 4;

	for (int i = 0; i < 12; i++)
		for (int j = 0; j < 12; j++)
			run(v[i], v[j]);
	H(__sync_fetch_and_max(&iv, -5)); H(__sync_fetch_and_min(&iv, -5)); H(iv);
	H(__sync_fetch_and_umax(&uv, -5)); H(__sync_fetch_and_umin(&uv, 9)); H(uv);
	H(__sync_fetch_and_nand(&iv, 6)); H(__sync_nand_and_fetch(&iv, 6)); H(iv);
	H(__atomic_test_and_set(&flag, 5)); H(__atomic_test_and_set(&flag, 5)); H(flag);
	__atomic_clear(&flag, 5); H(flag);
	H(__c11_atomic_compare_exchange_strong(&ai, &e, 9, 5, 5)); H(e); H(ai);
	e = 4; H(__c11_atomic_compare_exchange_strong(&ai, &e, 9, 5, 5)); H(e); H(ai);
	while (!__c11_atomic_compare_exchange_weak(&ai, &e, 11, 5, 5))
		;
	H(ai); H(__c11_atomic_fetch_max(&ai, 20, 5)); H(__c11_atomic_fetch_min(&ai, 2, 5)); H(__c11_atomic_fetch_nand(&ai, 3, 5)); H(ai);
	H(__c11_atomic_is_lock_free(4));
	printf("%llx\n", (unsigned long long)h);
	return 0;
}
