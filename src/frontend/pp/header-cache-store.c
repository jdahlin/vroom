/* vcc preprocessor: header cache: the shared store of deduplicated chunks and blobs */
#include "pp-internal.h"
#include "port/crc32c.h"

/* ---------------- deduplicated chunks and blobs ---------------- */

VPRIV void *pp_header_cache_allocate(pp *p, size_t n)
{
	p->entry_bytes += (n + 15) & ~(size_t)15;
	return pp_shared_allocate(n);
}

#define PP_HEADER_CACHE_CHUNK_TABLE_BITS 22
static _Atomic(const pp_header_cache_chunk *) *_Atomic pp_header_cache_chunk_table;
static pthread_once_t pp_header_cache_chunk_once = PTHREAD_ONCE_INIT;

static void pp_header_cache_chunk_table_init(void)
{
	atomic_store_explicit(&pp_header_cache_chunk_table, pp_address_space_reserve(sizeof(_Atomic(const pp_header_cache_chunk *)) << PP_HEADER_CACHE_CHUNK_TABLE_BITS), memory_order_release);
}

static uint64_t pp_header_cache_chunk_hash(const void *s, size_t n, uint32_t esz)
{
	const unsigned char *b = s;
	uint32_t h0 = (uint32_t)n, h1 = esz;
	size_t i = 0;

	for (; i + 16 <= n; i += 16) {
		uint64_t w0, w1;

		memcpy(&w0, b + i, 8);
		memcpy(&w1, b + i + 8, 8);
		h0 = __crc32cd(h0, w0);
		h1 = __crc32cd(h1, w1);
	}
	for (; i < n; i++)
		h0 = __crc32cb(h0, b[i]);
	return ((uint64_t)h0 << 32 | h1) * 0x9E3779B97F4A7C15ull;
}

/* the shared chunk holding n elements of size esz equal to a (stored once) */
static const pp_header_cache_chunk *pp_header_cache_chunk_get(pp *p, const void *a, uint32_t n, uint32_t esz)
{
	size_t bytes = (size_t)n * esz;
	uint64_t h = pp_header_cache_chunk_hash(a, bytes, esz);
	pp_header_cache_chunk *mine = NULL;

	if (unlikely(!atomic_load_explicit(&pp_header_cache_chunk_table, memory_order_acquire)))
		pthread_once(&pp_header_cache_chunk_once, pp_header_cache_chunk_table_init);
	const pp_header_cache_chunk **cc = &p->chunk_cache_by_hash[h >> 52];
	_Atomic(const pp_header_cache_chunk *) *tab;

	if (*cc && (*cc)->h == h && (*cc)->n == n && (*cc)->element_size == esz && !memcmp((*cc)->d, a, bytes))
		return *cc;
	tab = atomic_load_explicit(&pp_header_cache_chunk_table, memory_order_relaxed);

	for (uint32_t i = (uint32_t)(h >> (64 - PP_HEADER_CACHE_CHUNK_TABLE_BITS)), k = 0; k < 64; i = (i + 1) & ((1u << PP_HEADER_CACHE_CHUNK_TABLE_BITS) - 1), k++) {
		const pp_header_cache_chunk *e = atomic_load_explicit(&tab[i], memory_order_acquire);

		if (!e) {
			if (!mine) {
				mine = pp_header_cache_allocate(p, sizeof(pp_header_cache_chunk) + bytes + 16);
				mine->h = h;
				mine->n = n;
				mine->element_size = esz;
				memcpy(mine->d, a, bytes);
				memset(mine->d + bytes, 0, 16);
			}
			if (atomic_compare_exchange_strong_explicit(&tab[i], &e, mine, memory_order_acq_rel,
								    memory_order_acquire))
				return *cc = mine;
		}
		if (e->h == h && e->n == n && e->element_size == esz && !memcmp(e->d, a, bytes))
			return *cc = e;	/* (a chunk of ours that lost a race stays unused) */
	}
	if (!mine) {
		mine = pp_header_cache_allocate(p, sizeof(pp_header_cache_chunk) + bytes + 16);
		mine->h = h;
		mine->n = n;
		mine->element_size = esz;
		memcpy(mine->d, a, bytes);
		memset(mine->d + bytes, 0, 16);
	}
	return mine;	/* table crowded here: not shared */
}

/* n bytes equal to s, NUL and 15 more zero bytes after them, stored once */
VPRIV const void *pp_header_cache_blob_get(pp *p, const void *s, size_t n)
{
	if (n >= 0xffffffffu) {
		char *d = pp_header_cache_allocate(p, n + 16);

		memcpy(d, s, n);
		memset(d + n, 0, 16);
		return d;
	}
	return pp_header_cache_chunk_get(p, s, (uint32_t)n, 1)->d;
}

/*
 * Elements whose first 4 bytes are an id, cut into shared chunks after
 * every id whose hash has its low 5 bits clear (~32 per chunk, at most 256).
 */
VPRIV pp_header_cache_vector pp_header_cache_vector_store(pp *p, const void *a, uint32_t n, uint32_t esz)
{
	const char *b = a;
	const pp_header_cache_chunk **c, **tmp;
	uint32_t nc = 0, st = 0;
	pp_header_cache_vector v = { NULL, 0, n };

	if (!n)
		return v;
	tmp = varena_alloc(&p->temporary_region, (size_t)n * sizeof(*tmp));
	for (uint32_t i = 0; i < n; i++) {
		uint32_t id;

		memcpy(&id, b + (size_t)i * esz, 4);
		if (((id * 0x9E3779B1u) >> 27) == 0 || i + 1 - st == 256 || i + 1 == n) {
			/* a chunk this TU met lately, found by its ids before any hashing */
			uint32_t m = i + 1 - st, id0;
			const char *q = b + (size_t)st * esz;
			const pp_header_cache_chunk **vc;

			memcpy(&id0, q, 4);
			vc = &p->chunk_cache_by_ids[(((uint64_t)id0 * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)id * 0xc2b2ae3d27d4eb4full) ^
				      ((uint64_t)m << 8 | esz)) * 0xff51afd7ed558ccdull >> 52];
			if (!*vc || (*vc)->n != m || (*vc)->element_size != esz || memcmp((*vc)->d, q, (size_t)m * esz))
				*vc = pp_header_cache_chunk_get(p, q, m, esz);
			tmp[nc++] = *vc;
			st = i + 1;
		}
	}
	c = pp_header_cache_allocate(p, nc * sizeof(*c));
	memcpy(c, tmp, nc * sizeof(*c));
	v.chunks = c;
	v.chunk_count = nc;
	return v;
}
