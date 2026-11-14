/* vcc-flags: -O2 */
/* SVE through vcc's arm_sve.h: PostgreSQL's pg_popcount_sve and its masked
 * variant (src/port/pg_popcount_aarch64.c), any vector length, against a
 * bit loop.  Sizeless locals, copies, predicates in a loop. */
#include <arm_sve.h>
int printf(const char *, ...);

__attribute__((target("arch=armv8-a+sve"), noinline))
static uint64_t popcount_sve(const char *buf, int bytes)
{
	svbool_t pred = svptrue_b64();
	svuint64_t accum1 = svdup_u64(0), accum2 = svdup_u64(0), accum3 = svdup_u64(0), accum4 = svdup_u64(0);
	uint32_t vec_len = svcntb(), bytes_per_iteration = 4 * vec_len;
	uint64_t popcnt = 0;

	for (; bytes >= bytes_per_iteration; bytes -= bytes_per_iteration) {
		svuint64_t vec;

		vec = svld1_u64(pred, (const uint64_t *)buf);
		accum1 = svadd_u64_x(pred, accum1, svcnt_u64_x(pred, vec));
		buf += vec_len;
		vec = svld1_u64(pred, (const uint64_t *)buf);
		accum2 = svadd_u64_x(pred, accum2, svcnt_u64_x(pred, vec));
		buf += vec_len;
		vec = svld1_u64(pred, (const uint64_t *)buf);
		accum3 = svadd_u64_x(pred, accum3, svcnt_u64_x(pred, vec));
		buf += vec_len;
		vec = svld1_u64(pred, (const uint64_t *)buf);
		accum4 = svadd_u64_x(pred, accum4, svcnt_u64_x(pred, vec));
		buf += vec_len;
	}
	bytes_per_iteration = 2 * vec_len;
	if (bytes >= bytes_per_iteration) {
		svuint64_t vec;

		vec = svld1_u64(pred, (const uint64_t *)buf);
		accum1 = svadd_u64_x(pred, accum1, svcnt_u64_x(pred, vec));
		buf += vec_len;
		vec = svld1_u64(pred, (const uint64_t *)buf);
		accum2 = svadd_u64_x(pred, accum2, svcnt_u64_x(pred, vec));
		buf += vec_len;
		bytes -= bytes_per_iteration;
	}
	popcnt += svaddv_u64(pred, svadd_u64_x(pred, accum1, accum2));
	popcnt += svaddv_u64(pred, svadd_u64_x(pred, accum3, accum4));
	for (; bytes > 0; bytes -= vec_len) {
		svuint8_t vec;

		pred = svwhilelt_b8_s32(0, bytes);
		vec = svld1_u8(pred, (const uint8_t *)buf);
		popcnt += svaddv_u8(pred, svcnt_u8_x(pred, vec));
		buf += vec_len;
	}
	return popcnt;
}

__attribute__((target("arch=armv8-a+sve"), noinline))
static uint64_t popcount_masked_sve(const char *buf, int bytes, uint8_t mask)
{
	svbool_t pred = svptrue_b64();
	svuint64_t accum1 = svdup_u64(0), accum2 = svdup_u64(0);
	uint32_t vec_len = svcntb(), bytes_per_iteration = 2 * vec_len;
	uint64_t popcnt = 0, mask64 = ~0ull / 0xFF * mask;

	for (; bytes >= bytes_per_iteration; bytes -= bytes_per_iteration) {
		svuint64_t vec;

		vec = svand_n_u64_x(pred, svld1_u64(pred, (const uint64_t *)buf), mask64);
		accum1 = svadd_u64_x(pred, accum1, svcnt_u64_x(pred, vec));
		buf += vec_len;
		vec = svand_n_u64_x(pred, svld1_u64(pred, (const uint64_t *)buf), mask64);
		accum2 = svadd_u64_x(pred, accum2, svcnt_u64_x(pred, vec));
		buf += vec_len;
	}
	popcnt += svaddv_u64(pred, svadd_u64_x(pred, accum1, accum2));
	for (; bytes > 0; bytes -= vec_len) {
		svuint8_t vec;

		pred = svwhilelt_b8_s32(0, bytes);
		vec = svand_n_u8_x(pred, svld1_u8(pred, (const uint8_t *)buf), mask);
		popcnt += svaddv_u8(pred, svcnt_u8_x(pred, vec));
		buf += vec_len;
	}
	return popcnt;
}

/* a sizeless value through a pointer and a conditional copy */
__attribute__((target("arch=armv8-a+sve"), noinline))
static uint64_t copies(uint64_t x, int pick)
{
	svuint64_t a = svdup_u64(x), b = svdup_u64(x + 1), c, *p = pick ? &a : &b;
	svbool_t all = svptrue_b64(), q = all;

	c = *p;
	*p = svadd_u64_x(q, c, c);
	return svaddv_u64(q, pick ? a : b) / (svcntb() / 8);
}

static uint64_t slow(const char *buf, int bytes, uint8_t mask)
{
	uint64_t n = 0;

	for (int i = 0; i < bytes; i++)
		for (int k = 0; k < 8; k++)
			n += ((uint8_t)buf[i] & mask) >> k & 1;
	return n;
}

static char data[3001];

int main(void)
{
	static const int sizes[] = { 0, 1, 7, 8, 15, 16, 17, 31, 63, 64, 65, 127, 255, 256, 257, 511, 1000, 1024, 2047, 3000 };
	uint32_t s = 12345;
	int bad = 0;

	for (int i = 0; i < 3001; i++) {
		s = s * 1103515245u + 12345u;
		data[i] = (char)(s >> 16);
	}
	for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
		int n = sizes[i];
		uint64_t a = popcount_sve(data + 1, n), b = popcount_masked_sve(data + 1, n, 0x5a);

		if (a != slow(data + 1, n, 0xff) || b != slow(data + 1, n, 0x5a))
			bad++;
		printf("%d: %lu %lu\n", n, (unsigned long)a, (unsigned long)b);
	}
	printf("copies %lu %lu\n", (unsigned long)copies(20, 1), (unsigned long)copies(20, 0));
	printf("bad %d\n", bad);
	return bad != 0;
}
