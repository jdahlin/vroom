/*
 * String hashes for hash tables, 8 bytes per step with multiply-xorshift
 * mixing.  Two variants, from the object writer and from vobjcopy's string
 * table; they give different values, and each table keeps its own until
 * they are made one.
 */
#ifndef VCC_HASH_H
#define VCC_HASH_H

#include <stdint.h>
#include <string.h>

/* the object writer's symbol table: the tail from whole loads; never 0 */
static inline uint32_t vhash_obj(const char *s, uint32_t n)
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

/* vobjcopy's string table */
static inline uint32_t vhash_strtab(const char *s, uint32_t n)
{
	/* 8 bytes at a time, multiply-xorshift mixing */
	uint64_t h = 0x9e3779b97f4a7c15ull ^ n;
	uint32_t i = 0;
	for (; i + 8 <= n; i += 8) {
		uint64_t w;
		memcpy(&w, s + i, 8);
		h = (h ^ w) * 0xff51afd7ed558ccdull;
		h ^= h >> 29;
	}
	uint64_t w = 0;
	memcpy(&w, s + i, n - i);
	h = (h ^ w) * 0xc4ceb9fe1a85ec53ull;
	return (uint32_t)(h ^ h >> 32);
}

#endif
