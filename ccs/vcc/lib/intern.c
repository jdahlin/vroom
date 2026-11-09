#include <arm_acle.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "intern.h"
#include "arena.h"

#define SLOT_BITS 23	/* 64 MB of slots, touched as ids arrive */
#define NSLOT (1u << SLOT_BITS)
#define MAX_ENT (NSLOT / 4 * 3)
_Static_assert(MAX_ENT <= (1u << VINTERN_ID_BITS), "ids must fit the id-indexed tables");

#define CL8(n) ((n) < 0 ? 0 : (n) > 8 ? 8 : (n))
#define BM(n) (((1ull << (4 * CL8(n))) << (4 * CL8(n))) - 1)	/* the low n (0..8) bytes */
#define WM(n) { BM(n), BM((n) - 8) }
const uint64_t vintern_wmask[17][2] = {
	WM(0), WM(1), WM(2), WM(3), WM(4), WM(5), WM(6), WM(7), WM(8),
	WM(9), WM(10), WM(11), WM(12), WM(13), WM(14), WM(15), WM(16),
};
#undef WM
#undef BM
#undef CL8

static _Atomic uint64_t *slots;
vident *vintern_ents;
static _Atomic uint32_t nent = 1;	/* id 0 = none */

static void *reserve(size_t n) { return vreserve(n, 0, "vcc: mmap"); }

/* little-endian load of the first n (< 8) bytes of the word at s */
static inline uint64_t tail(const char *s, size_t n)
{
	uint64_t w;

	memcpy(&w, s, 8);
	return n ? w & (~0ull >> (64 - 8 * n)) : 0;
}

uint32_t vintern_hash(const char *s, size_t len)
{
	uint64_t w0, w1, w;
	uint32_t h = (uint32_t)len;
	size_t i;

	if (len <= 16) {
		/* as vintern_fast: always two words, zero padded */
		vintern_words(s, len, &w0, &w1);
		return __crc32cd(__crc32cd(h, w0), w1) * 0x9E3779B1u;
	}
	for (i = 0; i + 8 <= len; i += 8) {
		memcpy(&w, s + i, 8);
		h = __crc32cd(h, w);
	}
	if (i < len)
		h = __crc32cd(h, tail(s + i, len - i));
	/* slot index uses the high bits: mix */
	return h * 0x9E3779B1u;
}

static inline int same(const char *a, const char *b, size_t len)
{
	uint64_t x, y;
	size_t i;

	for (i = 0; i + 8 <= len; i += 8) {
		memcpy(&x, a + i, 8);
		memcpy(&y, b + i, 8);
		if (x != y)
			return 0;
	}
	return tail(a + i, len - i) == tail(b + i, len - i);
}

/* the probe from slot i on, inserting at the first empty slot */
static __attribute__((noinline)) uint32_t insert(const char *s, size_t len, uint32_t hash, uint32_t i,
						 uint64_t w0, uint64_t w1)
{
	uint32_t id = 0;

	for (;; i = (i + 1) & (NSLOT - 1)) {
		uint64_t v = atomic_load_explicit(&slots[i], memory_order_acquire);

		if (!v) {
			if (!id) {
				id = atomic_fetch_add_explicit(&nent, 1, memory_order_relaxed);
				if (id >= MAX_ENT) {
					fprintf(stderr, "vcc: identifier table full\n");
					abort();
				}
				vintern_ents[id] = (vident){ s, (uint32_t)len, hash, w0, w1 };
			}
			if (atomic_compare_exchange_strong_explicit(&slots[i], &v,
					(uint64_t)hash << 32 | id,
					memory_order_release, memory_order_acquire))
				return id;
			/* lost the race: v is the winner, compare with it */
		}
		if ((uint32_t)(v >> 32) == hash) {
			const vident *e = &vintern_ents[(uint32_t)v];

			if (e->len == len && e->w0 == w0 && e->w1 == w1 &&
			    (len <= 16 || same(e->s + 16, s + 16, len - 16)))
				return (uint32_t)v;	/* a reserved id, if any, is wasted */
		}
	}
}

#ifdef VLEX_STATS
extern unsigned long vlex_stat[512];
static uint32_t vintern_(const char *s, size_t len, uint32_t hash);
uint32_t vintern(const char *s, size_t len, uint32_t hash)
{
	uint64_t t0 = __builtin_readcyclecounter();
	uint32_t r = vintern_(s, len, hash);

	vlex_stat[321] += __builtin_readcyclecounter() - t0;
	return r;
}
#define vintern vintern_
static
#endif
/* a lookup loop without the insertion (most identifiers exist already) */
uint32_t vintern(const char *s, size_t len, uint32_t hash)
{
	uint32_t i = hash >> (32 - SLOT_BITS);
	uint64_t w0, w1;

	vintern_words(s, len, &w0, &w1);
	for (;; i = (i + 1) & (NSLOT - 1)) {
		uint64_t v = atomic_load_explicit(&slots[i], memory_order_acquire);

		if (!v)
			return insert(s, len, hash, i, w0, w1);
		if ((uint32_t)(v >> 32) == hash) {
			const vident *e = &vintern_ents[(uint32_t)v];

			if (e->len == len && e->w0 == w0 && e->w1 == w1 &&
			    (len <= 16 || same(e->s + 16, s + 16, len - 16)))
				return (uint32_t)v;
		}
	}
}

uint32_t vintern_long(const char *s, size_t len)
{
	return vintern(s, len, vintern_hash(s, len));
}

/* the id of s if interned, else 0 (no insert) */
uint32_t vintern_find(const char *s, size_t len, uint32_t hash)
{
	uint64_t w0, w1;

	vintern_words(s, len, &w0, &w1);
	for (uint32_t i = hash >> (32 - SLOT_BITS);; i = (i + 1) & (NSLOT - 1)) {
		uint64_t v = atomic_load_explicit(&slots[i], memory_order_acquire);

		if (!v)
			return 0;
		if ((uint32_t)(v >> 32) == hash) {
			const vident *e = &vintern_ents[(uint32_t)v];

			if (e->len == len && e->w0 == w0 && e->w1 == w1 &&
			    (len <= 16 || same(e->s + 16, s + 16, len - 16)))
				return (uint32_t)v;
		}
	}
}

/* bump allocator for copied spellings */
static _Atomic size_t copy_used;
static char *copy_buf;
#define COPY_MAX (256u << 20)

uint32_t vintern_copy(const char *s, size_t len)
{
	uint32_t h = vintern_hash(s, len), id = vintern_find(s, len, h);
	size_t off;
	char *d;

	if (id)
		return id;
	off = atomic_fetch_add(&copy_used, (len + 16) & ~(size_t)7);
	d = copy_buf + off;

	if (off + len + 16 > COPY_MAX)
		abort();
	memcpy(d, s, len);
	return vintern(d, len, h);
}

uint32_t vintern_cstr(const char *s)
{
	char b[80] = { 0 };
	size_t n = strlen(s);

	if (n + 16 > sizeof(b))
		abort();
	memcpy(b, s, n);
	return vintern_copy(b, n);
}

uint32_t vintern_count(void)
{
	return atomic_load(&nent) - 1;
}

static void init_once(void)
{
	static const char *const kw[] = {
#define X(k) #k,
		KEYWORDS(X)
#undef X
		"__VA_ARGS__", "__VA_OPT__",
	};

	/* lazily backed: only touched pages cost anything */
	slots = reserve(NSLOT * sizeof(*slots));
	vintern_ents = reserve(MAX_ENT * sizeof(*vintern_ents));
	copy_buf = reserve(COPY_MAX);
	for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) {
		uint32_t id = vintern_cstr(kw[i]);

		if (id != i + 1) {
			fprintf(stderr, "vcc: duplicate keyword %s\n", kw[i]);
			abort();
		}
	}
}

void vintern_init(void)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	pthread_once(&once, init_once);
}

vintern_cache *vintern_local(void)
{
	static _Thread_local vintern_cache *c;

	if (!c)
		c = reserve(sizeof(*c));
	return c;
}

