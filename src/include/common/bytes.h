/*
 * Unaligned loads and stores of integers in a byte stream, and alignment of
 * offsets.
 *
 * vbytes_load_u16/32/64 and vbytes_store_u16/32/64 read and write little-endian
 * values at any address (the formats of the targets, ELF and Mach-O, are little-endian);
 * the `_be` forms are big-endian (archive headers, wasm).  Both are correct on
 * either host byte order; the compiler turns them into one load or store.
 *
 * valign_up(v, a) rounds v up to a multiple of a.  valign_skew(v, a, skew)
 * rounds v up to the next value that is skew modulo a (a segment whose file
 * offset and address must agree modulo the page size).  Both are defined for
 * every a: 0 and 1 mean no alignment, and a need not be a power of two.
 */
#ifndef VCC_COMMON_BYTES_H
#define VCC_COMMON_BYTES_H

#include <stdint.h>
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define VBYTES_BIG_HOST_ 1
#else
#define VBYTES_BIG_HOST_ 0
#endif

static inline uint16_t vbytes_swap_u16_(uint16_t v) { return __builtin_bswap16(v); }
static inline uint32_t vbytes_swap_u32_(uint32_t v) { return __builtin_bswap32(v); }
static inline uint64_t vbytes_swap_u64_(uint64_t v) { return __builtin_bswap64(v); }

#define VBYTES_ACCESS_(bits) \
static inline uint##bits##_t vbytes_load_u##bits(const void *p) \
{ \
	uint##bits##_t v; \
	memcpy(&v, p, sizeof v); \
	return VBYTES_BIG_HOST_ ? vbytes_swap_u##bits##_(v) : v; \
} \
static inline uint##bits##_t vbytes_load_u##bits##_be(const void *p) \
{ \
	uint##bits##_t v; \
	memcpy(&v, p, sizeof v); \
	return VBYTES_BIG_HOST_ ? v : vbytes_swap_u##bits##_(v); \
} \
static inline void vbytes_store_u##bits(void *p, uint##bits##_t v) \
{ \
	v = VBYTES_BIG_HOST_ ? vbytes_swap_u##bits##_(v) : v; \
	memcpy(p, &v, sizeof v); \
} \
static inline void vbytes_store_u##bits##_be(void *p, uint##bits##_t v) \
{ \
	v = VBYTES_BIG_HOST_ ? v : vbytes_swap_u##bits##_(v); \
	memcpy(p, &v, sizeof v); \
}

VBYTES_ACCESS_(16)
VBYTES_ACCESS_(32)
VBYTES_ACCESS_(64)
#undef VBYTES_ACCESS_

static inline uint64_t valign_up(uint64_t v, uint64_t a)
{
	return a <= 1 ? v : (v + a - 1) / a * a;
}

static inline uint64_t valign_skew(uint64_t v, uint64_t a, uint64_t skew)
{
	if (a <= 1)
		return v;
	skew %= a;
	return (v + a - 1 - skew) / a * a + skew;
}

#endif
