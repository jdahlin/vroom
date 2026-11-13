/* Unit test of common/vector.h: heap and region growth, widths of the capacity, the old arena form. */
#include <fcntl.h>
#include <setjmp.h>
#include "check.h"
#include "common/vector.h"

typedef struct { uint8_t *v; uint16_t n, cap; } small16;
typedef struct { uint64_t *v; size_t n, cap; } wide;

static unsigned char pool[1 << 16];
static size_t pool_used, pool_calls;
static void *pool_alloc(size_t n)
{
	void *p = pool + pool_used;

	pool_used += (n + 15) & ~(size_t)15;
	pool_calls++;
	return p;
}

int main(void)
{
	wide w = { 0 };
	VVECTOR(int) a = { 0 };
	small16 s = { 0 };
	jmp_buf jb;

	for (uint64_t i = 0; i < 1000; i++)
		VVECTOR_APPEND(w.v, w.n, w.cap, i * 3);
	CHECK(w.n == 1000 && w.cap >= 1000 && (w.cap & (w.cap - 1)) == 0 && w.cap >= 8);
	for (uint64_t i = 0; i < 1000; i++)
		CHECK(w.v[i] == i * 3);
	VVECTOR_NEED(w.v, w.n, w.cap, 5000);
	CHECK(w.cap >= 6000 && w.v[999] == 2997);
	free(w.v);

	for (int i = 0; i < 100; i++)
		VVECTOR_APPEND(a.v, a.n, a.cap, i);
	CHECK(a.n == 100 && a.v[99] == 99 && a.cap == 128);
	free(a.v);

	/* a 16-bit capacity: grows to 32768, and the request past it fails the job instead of wrapping */
	for (int i = 0; i < 40000; i++) {
		if (s.n == 32768)
			break;
		VVECTOR_APPEND(s.v, s.n, s.cap, (uint8_t)i);
	}
	CHECK(s.n == 32768 && s.cap == 32768);
	vjob_fail_fd = open("/dev/null", 1);
	vjob_jb = &jb;
	if (!setjmp(jb)) {
		VVECTOR_APPEND(s.v, s.n, s.cap, 1);	/* needs 65536: not representable */
		CHECK(!"the overflowing append returned");
	} else {
		CHECK(s.n == 32768);
	}
	vjob_jb = NULL;
	free(s.v);

	{	/* the _IN forms: memory from alloc, old contents copied */
		VVECTOR(long) r = { 0 };

		for (long i = 0; i < 50; i++)
			VVECTOR_APPEND_IN(pool_alloc, r.v, r.n, r.cap, i * i);
		CHECK(r.n == 50 && r.cap == 64 && pool_calls == 4);	/* 8, 16, 32, 64 */
		for (long i = 0; i < 50; i++)
			CHECK(r.v[i] == i * i);
	}
	{	/* the older arena form */
		VVEC(int) o = { 0 };

		for (int i = 0; i < 20; i++)
			VVEC_PUSH(o, i, pool_alloc);
		CHECK(o.n == 20 && o.v[19] == 19);
	}
	return check_failures;
}
