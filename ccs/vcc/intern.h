#ifndef VCC_INTERN_H
#define VCC_INTERN_H
/*
 * Identifier interning, shared by all threads.
 *
 * Open addressing over 2^20 64-bit slots (hash << 32 | id), inserted with a
 * single CAS; lookups take no lock.  Hashing is CRC32C over 8-byte words.
 * Entries point at the spelling in the (immortal) source mapping, so
 * interning copies nothing except for identifiers that contain splices.
 *
 * Ids 1..KW_COUNT are the keywords and directive names below, in order, so
 * the preprocessor and parser can switch on them.
 */
#include <stddef.h>
#include <stdint.h>

#define KEYWORDS(X) \
	X(auto) X(break) X(case) X(char) X(const) X(continue) X(default) X(do) \
	X(double) X(else) X(enum) X(extern) X(float) X(for) X(goto) X(if) \
	X(inline) X(int) X(long) X(register) X(restrict) X(return) X(short) \
	X(signed) X(sizeof) X(static) X(struct) X(switch) X(typedef) X(union) \
	X(unsigned) X(void) X(volatile) X(while) X(_Alignas) X(_Alignof) \
	X(_Atomic) X(_Bool) X(_Generic) X(_Noreturn) X(_Static_assert) \
	X(_Thread_local) X(__attribute__) X(__attribute) X(asm) X(__asm__) \
	X(__asm) X(typeof) X(__typeof__) X(__typeof) X(typeof_unqual) \
	X(__typeof_unqual__) X(__inline__) X(__inline) X(__volatile__) \
	X(__volatile) X(__const__) X(__const) X(__signed__) X(__signed) \
	X(__restrict__) X(__restrict) X(__extension__) X(__alignof__) \
	X(__alignof) X(__auto_type) X(__label__) X(__builtin_va_arg) \
	X(__builtin_offsetof) X(__builtin_types_compatible_p) \
	X(__builtin_choose_expr) X(__int128) X(_Float16) X(__fp16) \
	X(__thread) X(__real__) X(__imag__) X(_Complex) X(static_assert) \
	X(bool) X(true) X(false) X(nullptr) X(alignof) X(alignas) \
	X(thread_local) \
	/* preprocessor */ \
	X(define) X(undef) X(include) X(include_next) X(ifdef) X(ifndef) \
	X(elif) X(elifdef) X(elifndef) X(endif) X(line) X(error) X(warning) \
	X(pragma) X(defined) X(_Pragma) \
	X(__has_include) X(__has_include_next) X(__has_attribute) \
	X(__has_builtin) X(__has_feature) X(__has_extension) \
	X(__has_c_attribute) X(__has_cpp_attribute) X(__is_identifier) \
	X(__has_declspec_attribute) X(__FILE__) X(__LINE__) X(__COUNTER__) \
	X(__DATE__) X(__TIME__) X(__BASE_FILE__) X(__FILE_NAME__) \
	X(__INCLUDE_LEVEL__) X(__TIMESTAMP__) X(once)

enum {
	KW_NONE,
#define X(k) KW_##k,
	KEYWORDS(X)
#undef X
	KW_VA_ARGS,	/* __VA_ARGS__ */
	KW_VA_OPT,	/* __VA_OPT__ */
	KW_END
};
#define KW_COUNT (KW_END - 1)

typedef struct vident {
	const char *s;
	uint32_t len;
	uint32_t hash;
	uint64_t w0, w1;	/* the first 16 bytes, zero padded */
} vident;

/* the interned identifiers, indexed by id (do not hold across inserts) */
extern vident *vintern_ents;

void vintern_init(void);
uint32_t vintern_hash(const char *s, size_t len);
/* s must be immortal and readable to s[len + 7] */
uint32_t vintern(const char *s, size_t len, uint32_t hash);
/* the id of s if already interned, else 0 (s readable to s[16]) */
uint32_t vintern_find(const char *s, size_t len, uint32_t hash);
/* copies s unless interned (for spellings that are not immortal; s readable to s[16]) */
uint32_t vintern_copy(const char *s, size_t len);
uint32_t vintern_count(void);

/*
 * Per-thread front cache: direct mapped, identifiers of up to 16 bytes kept
 * inline, so a hit compares two words in L1 and touches neither the shared
 * table nor the spelling.  Hashes agree with vintern_hash().
 */
#include <arm_acle.h>
#include <string.h>

/* identifier ids are below 1 << VINTERN_ID_BITS: tables indexed by id are sized by it */
#define VINTERN_ID_BITS 23

#define VIC_BITS 11
typedef struct vintern_cache {
	struct { uint64_t w0, w1; uint32_t hash, id; uint64_t _pad; } e[1u << VIC_BITS];
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
		return vintern(s, len, vintern_hash(s, len));
	}
	vintern_words16(s, len, &w0, &w1);
	h = __crc32cd(__crc32cd((uint32_t)len, w0), w1) * 0x9E3779B1u;
	{
		__typeof__(&c->e[0]) e = &c->e[h & ((1u << VIC_BITS) - 1)];

		VI_STAT(0);
		/* identifiers hold no NUL: the zero-padded words determine len */
		if (((e->w0 ^ w0) | (e->w1 ^ w1)) == 0)
			return e->id;
		VI_STAT(1);
		e->id = vintern(s, len, h);
		e->hash = h;
		e->w0 = w0;
		e->w1 = w1;
		return e->id;
	}
}

#endif
