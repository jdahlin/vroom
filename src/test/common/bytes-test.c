/* Unit test of common/bytes.h: both byte orders at odd addresses, alignment including 0. */
#include "check.h"
#include "common/bytes.h"

int main(void)
{
	uint8_t b[16] = { 0 };

	vbytes_store_u16(b + 1, 0x1234);
	CHECK(b[1] == 0x34 && b[2] == 0x12 && vbytes_load_u16(b + 1) == 0x1234);
	vbytes_store_u32(b + 3, 0x01020304);
	CHECK(b[3] == 4 && b[6] == 1 && vbytes_load_u32(b + 3) == 0x01020304);
	vbytes_store_u64(b + 7, 0x0102030405060708ull);
	CHECK(b[7] == 8 && b[14] == 1 && vbytes_load_u64(b + 7) == 0x0102030405060708ull);
	vbytes_store_u16_be(b + 1, 0x1234);
	CHECK(b[1] == 0x12 && b[2] == 0x34 && vbytes_load_u16_be(b + 1) == 0x1234);
	vbytes_store_u32_be(b + 3, 0x01020304);
	CHECK(b[3] == 1 && b[6] == 4 && vbytes_load_u32_be(b + 3) == 0x01020304);
	vbytes_store_u64_be(b + 7, 0x0102030405060708ull);
	CHECK(b[7] == 1 && b[14] == 8 && vbytes_load_u64_be(b + 7) == 0x0102030405060708ull);
	CHECK(vbytes_load_u32_be((const uint8_t[]){ 0xde, 0xad, 0xbe, 0xef }) == 0xdeadbeef);
	CHECK(vbytes_load_u32((const uint8_t[]){ 0xef, 0xbe, 0xad, 0xde }) == 0xdeadbeef);

	CHECK(valign_up(0, 8) == 0 && valign_up(1, 8) == 8 && valign_up(8, 8) == 8 && valign_up(9, 8) == 16);
	CHECK(valign_up(10, 0) == 10 && valign_up(10, 1) == 10);
	CHECK(valign_up(10, 6) == 12);	/* not a power of two */
	CHECK(valign_up(~(uint64_t)0 - 15, 16) == ~(uint64_t)0 - 15);
	CHECK(valign_skew(5, 0, 7) == 5 && valign_skew(5, 1, 7) == 5);
	CHECK(valign_skew(0x1000, 0x1000, 0x234) == 0x1234);
	CHECK(valign_skew(0x1234, 0x1000, 0x234) == 0x1234);
	CHECK(valign_skew(0x1235, 0x1000, 0x234) == 0x2234);
	CHECK(valign_skew(0x1235, 0x1000, 0x1234) == 0x2234);	/* skew is taken modulo a */
	return check_failures;
}
