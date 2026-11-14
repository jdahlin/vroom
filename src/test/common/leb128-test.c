/* Unit test of common/leb128.h: the specification's examples, round trips, padded forms, truncated and overlong input. */
#include <string.h>
#include "check.h"
#include "common/leb128.h"

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint64_t next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

int main(void)
{
	uint8_t b[16];
	const uint8_t *p;
	uint64_t u;
	int64_t s;

	/* DWARF 7.6 examples */
	CHECK(vleb128_unsigned_store(b, 2) == 1 && b[0] == 2);
	CHECK(vleb128_unsigned_store(b, 127) == 1 && b[0] == 127);
	CHECK(vleb128_unsigned_store(b, 128) == 2 && b[0] == 0x80 && b[1] == 1);
	CHECK(vleb128_unsigned_store(b, 624485) == 3 && !memcmp(b, "\xe5\x8e\x26", 3));
	CHECK(vleb128_signed_store(b, -2) == 1 && b[0] == 0x7e);
	CHECK(vleb128_signed_store(b, 127) == 2 && b[0] == 0xff && b[1] == 0);
	CHECK(vleb128_signed_store(b, -127) == 2 && b[0] == 0x81 && b[1] == 0x7f);
	CHECK(vleb128_signed_store(b, -128) == 2 && b[0] == 0x80 && b[1] == 0x7f);
	CHECK(vleb128_signed_store(b, -123456) == 3 && !memcmp(b, "\xc0\xbb\x78", 3));
	CHECK(vleb128_unsigned_store(b, UINT64_MAX) == 10 && vleb128_signed_store(b, INT64_MIN) == 10);

	for (int i = 0; i < 20000; i++) {
		uint64_t v = next() >> (next() % 64);
		int64_t sv = (int64_t)v * ((i & 1) ? -1 : 1);
		size_t n = vleb128_unsigned_store(b, v);

		CHECK(n == vleb128_unsigned_length(v));
		p = b;
		CHECK(vleb128_unsigned_load(&p, b + n, &u) && u == v && p == b + n);
		n = vleb128_signed_store(b, sv);
		CHECK(n == vleb128_signed_length(sv));
		p = b;
		CHECK(vleb128_signed_load(&p, b + n, &s) && s == sv && p == b + n);
		/* padded: the same value in 10 bytes loads back; as long as the shortest form for short ones */
		vleb128_unsigned_store_padded(b, v, 10);
		p = b;
		CHECK(vleb128_unsigned_load(&p, b + 10, &u) && u == v && p == b + 10);
		vleb128_signed_store_padded(b, sv, 10);
		p = b;
		CHECK(vleb128_signed_load(&p, b + 10, &s) && s == sv && p == b + 10);
	}
	vleb128_unsigned_store_padded(b, 3, 5);
	CHECK(!memcmp(b, "\x83\x80\x80\x80\x00", 5));
	vleb128_signed_store_padded(b, -1, 3);
	CHECK(!memcmp(b, "\xff\xff\x7f", 3));

	/* truncated: the bytes end inside a value */
	b[0] = 0x80; b[1] = 0x80;
	p = b;
	CHECK(!vleb128_unsigned_load(&p, b + 2, &u) && p == b + 2);
	p = b;
	CHECK(!vleb128_signed_load(&p, b + 0, &s));
	/* overlong: 12 bytes of continuation, the bits beyond 64 dropped, no overread */
	memset(b, 0xff, 11);
	b[11] = 0x01;
	p = b;
	CHECK(vleb128_unsigned_load(&p, b + 12, &u) && u == UINT64_MAX && p == b + 12);
	return check_failures;
}
