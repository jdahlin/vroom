/*
 * LEB128 (DWARF, WebAssembly): variable-length integers, 7 bits per byte,
 * low bits first, the high bit of a byte says another follows.
 *
 *   vleb128_unsigned_store(p, v), vleb128_signed_store(p, v)
 *       write the shortest form at p (at most 10 bytes) and return its length
 *   vleb128_unsigned_store_padded(p, v, n), vleb128_signed_store_padded(p, v, n)
 *       write exactly n bytes (a value patched in later: wasm section sizes
 *       are 5 bytes); v must fit in 7 * n bits, as a signed value for the
 *       signed form
 *   vleb128_unsigned_load(&p, end, &v), vleb128_signed_load(&p, end, &v)
 *       read one value and advance p past it; false (p at end) when the bytes
 *       end before the last one.  Bits beyond 64 are dropped
 *   vleb128_unsigned_length(v), vleb128_signed_length(v)
 *       the length of the shortest form
 *
 * Stores take a byte pointer; to append to a vbuffer, store into a 10-byte
 * temporary and append that many bytes.
 */
#ifndef VCC_COMMON_LEB128_H
#define VCC_COMMON_LEB128_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline size_t vleb128_unsigned_store(uint8_t *p, uint64_t v)
{
	size_t n = 0;

	do {
		uint8_t c = v & 0x7f;

		v >>= 7;
		p[n++] = c | (v ? 0x80 : 0);
	} while (v);
	return n;
}

static inline size_t vleb128_signed_store(uint8_t *p, int64_t v)
{
	size_t n = 0;

	for (;;) {
		uint8_t c = v & 0x7f;

		v >>= 7;	/* arithmetic shift */
		if ((v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40))) {
			p[n++] = c;
			return n;
		}
		p[n++] = c | 0x80;
	}
}

static inline void vleb128_unsigned_store_padded(uint8_t *p, uint64_t v, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		p[i] = (v & 0x7f) | (i + 1 < n ? 0x80 : 0);
		v >>= 7;
	}
}

static inline void vleb128_signed_store_padded(uint8_t *p, int64_t v, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		p[i] = (v & 0x7f) | (i + 1 < n ? 0x80 : 0);
		v >>= 7;
	}
}

static inline bool vleb128_unsigned_load(const uint8_t **p, const uint8_t *end, uint64_t *out)
{
	const uint8_t *q = *p;
	uint64_t v = 0;
	unsigned shift = 0;
	uint8_t c;

	do {
		if (q >= end) {
			*p = end;
			return false;
		}
		c = *q++;
		if (shift < 64)
			v |= (uint64_t)(c & 0x7f) << shift;
		shift += 7;
	} while (c & 0x80);
	*p = q;
	*out = v;
	return true;
}

static inline bool vleb128_signed_load(const uint8_t **p, const uint8_t *end, int64_t *out)
{
	const uint8_t *q = *p;
	uint64_t v = 0;
	unsigned shift = 0;
	uint8_t c;

	do {
		if (q >= end) {
			*p = end;
			return false;
		}
		c = *q++;
		if (shift < 64)
			v |= (uint64_t)(c & 0x7f) << shift;
		shift += 7;
	} while (c & 0x80);
	if (shift < 64 && (c & 0x40))
		v |= ~(uint64_t)0 << shift;
	*p = q;
	*out = (int64_t)v;
	return true;
}

static inline size_t vleb128_unsigned_length(uint64_t v)
{
	uint8_t scratch[10];

	return vleb128_unsigned_store(scratch, v);
}

static inline size_t vleb128_signed_length(int64_t v)
{
	uint8_t scratch[10];

	return vleb128_signed_store(scratch, v);
}

#endif
