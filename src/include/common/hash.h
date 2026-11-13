/*
 * The string hash of the tools' tables (the object writer's symbols, vobjcopy's
 * string table): 8 bytes per step with multiply-xorshift mixing.  No output
 * byte depends on the value (a build with another mix gives identical
 * objects and identical vobjcopy output), so a table may be built on it
 * freely.  Not for digests (digest.h).
 */
#ifndef VCC_COMMON_HASH_H
#define VCC_COMMON_HASH_H

#include <stdint.h>
#include <string.h>

/* the tail from whole loads (no byte-wise read past the end); never 0 */
static inline uint32_t vhash_string(const char *s, uint32_t n)
{
	uint64_t h = 0x9e3779b97f4a7c15ull ^ n, w;
	const char *e = s + n;
	uint32_t n0 = n, r = n & 7;
	while (n >= 8) { memcpy(&w, s, 8); h = (h ^ w) * 0xff51afd7ed558ccdull; h ^= h >> 32; s += 8; n -= 8; }
	/* the tail (n % 8 bytes, little-endian in the low bits, as a
	 * memcpy into a zeroed word would put them) from whole loads */
	if (r && n0 >= 8) { memcpy(&w, e - 8, 8); w >>= 8 * (8 - r); }
	else if (r >= 4) { uint32_t x, y; memcpy(&x, s, 4); memcpy(&y, e - 4, 4); w = x | (uint64_t)y << (8 * (r - 4)); }
	else if (r) w = (uint64_t)(uint8_t)s[0] | (uint64_t)(uint8_t)s[r / 2] << (8 * (r / 2)) | (uint64_t)(uint8_t)e[-1] << (8 * (r - 1));
	else w = 0;
	h = (h ^ w) * 0xc4ceb9fe1a85ec53ull; h ^= h >> 29;
	return (uint32_t)h | 1;
}

#endif
