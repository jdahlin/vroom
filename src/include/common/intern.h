#ifndef VCC_COMMON_INTERN_H
#define VCC_COMMON_INTERN_H
/*
 * Identifier interning, shared by all threads.
 *
 * Open addressing over 2^23 64-bit slots (hash << 32 | id), inserted with a
 * single CAS; lookups take no lock.  Hashing is CRC32C over 8-byte words.
 * Entries point at the spelling in the (immortal) source mapping, so
 * interning copies nothing except for identifiers that contain splices.
 *
 * The first ids are the names a language registers with vintern_preset (for C:
 * frontend/c/keywords.h), in order, so the preprocessor and parser can switch
 * on them.
 */
#include <stddef.h>
#include <stdint.h>

typedef struct vident {
	const char *s;
	uint32_t len;
	uint32_t hash;
	uint64_t w0, w1;	/* the first 16 bytes, zero padded */
} vident;

/* the interned identifiers, indexed by id (do not hold across inserts) */
extern vident *vintern_ents;

/* the names that take ids 1..n, in order: call before the first vintern_init */
void vintern_preset(const char *const *names, size_t n);
void vintern_init(void);
uint32_t vintern_hash(const char *s, size_t len);
/* s must be immortal and readable to s[len + 7] */
uint32_t vintern(const char *s, size_t len, uint32_t hash);
/* vintern(s, len, vintern_hash(s, len)) in one call */
uint32_t vintern_long(const char *s, size_t len);
/* the id of s if already interned, else 0 (s readable to s[16]) */
uint32_t vintern_find(const char *s, size_t len, uint32_t hash);
/* copies s unless interned (for spellings that are not immortal; s readable to s[16]) */
uint32_t vintern_copy(const char *s, size_t len);
/* vintern_copy of a NUL-terminated string with no padding (< 64 bytes) */
uint32_t vintern_cstr(const char *s);
uint32_t vintern_count(void);

/*
 * Per-thread front cache: direct mapped, identifiers of up to 16 bytes kept
 * inline, so a hit compares two words in L1 and touches neither the shared
 * table nor the spelling.  Hashes agree with vintern_hash().
 */
#include "port/crc32c.h"
#include <string.h>

/* identifier ids are below 1 << VINTERN_ID_BITS: tables indexed by id are sized by it */
#define VINTERN_ID_BITS 23

#define VINTERN_CACHE_BITS 11
typedef struct vintern_cache {
	struct { uint64_t w0, w1; uint32_t _hash, id; uint64_t _pad; } e[1u << VINTERN_CACHE_BITS];
} vintern_cache;

#define unlikely_vi(x) __builtin_expect(!!(x), 0)

vintern_cache *vintern_local(void);

#ifdef VLEX_STATS
extern unsigned long vlex_stat[512];
#define VI_STAT(i) (vlex_stat[310 + (i)]++)
#else
#define VI_STAT(i) ((void)0)
#endif

/* vintern_wmask[len]: the bytes of the first 16 kept for len <= 16 */
extern const uint64_t vintern_wmask[17][2];

/* vintern_words for len <= 16: two loads and a mask from a table */
static inline __attribute__((always_inline)) void vintern_words16(const char *s, size_t len, uint64_t *w0, uint64_t *w1)
{
	memcpy(w0, s, 8);
	memcpy(w1, s + 8, 8);
	*w0 &= vintern_wmask[len][0];
	*w1 &= vintern_wmask[len][1];
}

/* the first 16 bytes of s, zero past len (s readable to s[16]) */
static inline __attribute__((always_inline)) void vintern_words(const char *s, size_t len, uint64_t *w0, uint64_t *w1)
{
	vintern_words16(s, len < 16 ? len : 16, w0, w1);
}

/* s must be readable to s[16]; no data-dependent branch for len <= 16 */
static inline __attribute__((always_inline)) uint32_t vintern_fast(vintern_cache *c, const char *s, size_t len)
{
	uint64_t w0, w1;
	uint32_t h;

	if (unlikely_vi(len > 16)) {
		VI_STAT(2);
		return vintern_long(s, len);
	}
	vintern_words16(s, len, &w0, &w1);
	h = vcrc32c_two((uint32_t)len, w0, w1) * 0x9E3779B1u;
	{
		__typeof__(&c->e[0]) e = &c->e[h & ((1u << VINTERN_CACHE_BITS) - 1)];

		VI_STAT(0);
		/* identifiers hold no NUL: the zero-padded words determine len */
		if (((e->w0 ^ w0) | (e->w1 ^ w1)) == 0)
			return e->id;
		VI_STAT(1);
		e->id = vintern(s, len, h);
		e->w0 = w0;
		e->w1 = w1;
		return e->id;
	}
}
#endif
