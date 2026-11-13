/*
 * CRC32C for host string hashes on x86-64 (hostcrc.h): the software and the
 * SSE4.2 implementation, chosen once at start-up.  arm64 uses ACLE's
 * instructions and needs no file.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "port/crc32c.h"

#if defined(__x86_64__)
#include <nmmintrin.h>

static uint32_t crc_byte_soft(uint32_t h, uint8_t b)
{
	static const uint32_t nibble[16] = {
		0x00000000, 0x105ec76f, 0x20bd8ede, 0x30e349b1,
		0x417b1dbc, 0x5125dad3, 0x61c69362, 0x7198540d,
		0x82f63b78, 0x92a8fc17, 0xa24bb5a6, 0xb21572c9,
		0xc38d26c4, 0xd3d3e1ab, 0xe330a81a, 0xf36e6f75,
	};
	h ^= b;
	h = (h >> 4) ^ nibble[h & 15];
	return (h >> 4) ^ nibble[h & 15];
}

static uint32_t crc_word_soft(uint32_t h, uint64_t w)
{
	for (int i = 0; i < 8; i++, w >>= 8) h = crc_byte_soft(h, (uint8_t)w);
	return h;
}

static uint32_t crc_pair_soft(uint32_t h, uint64_t a, uint64_t b)
{
	return crc_word_soft(crc_word_soft(h, a), b);
}

__attribute__((target("sse4.2")))
static uint32_t crc_byte_hw(uint32_t h, uint8_t b) { return _mm_crc32_u8(h, b); }
__attribute__((target("sse4.2")))
static uint32_t crc_word_hw(uint32_t h, uint64_t w) { return (uint32_t)_mm_crc32_u64(h, w); }
__attribute__((target("sse4.2")))
static uint32_t crc_pair_hw(uint32_t h, uint64_t a, uint64_t b)
{
	return (uint32_t)_mm_crc32_u64(_mm_crc32_u64(h, a), b);
}

/* Safe even if another constructor hashes before our dispatch runs. */
uint32_t (*vcrc32cb)(uint32_t, uint8_t) = crc_byte_soft;
uint32_t (*vcrc32cd)(uint32_t, uint64_t) = crc_word_soft;
uint32_t (*vcrc32c_pair)(uint32_t, uint64_t, uint64_t) = crc_pair_soft;

__attribute__((constructor)) static void init_host_crc(void)
{
	const char *cap = getenv("VCC_CRC");
	__builtin_cpu_init();
	if (cap && !strcmp(cap, "software")) return;
	if (!__builtin_cpu_supports("sse4.2")) return;
	vcrc32cb = crc_byte_hw;
	vcrc32cd = crc_word_hw;
	vcrc32c_pair = crc_pair_hw;
}
#endif
