/* vcc preprocessor: header cache: the shared store of deduplicated chunks and blobs */
#include "ppimpl.h"
#include "port/crc32c.h"

/* ---------------- deduplicated chunks and blobs ---------------- */

VPRIV void *hsalloc(pp *p, size_t n)
{
	p->hc_bytes += (n + 15) & ~(size_t)15;
	return salloc(n);
}

#define HCK_BITS 22
static _Atomic(const hck *) *_Atomic hck_tab;
static pthread_once_t hck_once = PTHREAD_ONCE_INIT;

static void hck_init(void)
{
	atomic_store_explicit(&hck_tab, vm(sizeof(_Atomic(const hck *)) << HCK_BITS), memory_order_release);
}

static uint64_t hck_hash(const void *s, size_t n, uint32_t esz)
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
static const hck *hck_get(pp *p, const void *a, uint32_t n, uint32_t esz)
{
	size_t bytes = (size_t)n * esz;
	uint64_t h = hck_hash(a, bytes, esz);
	hck *mine = NULL;

	if (unlikely(!atomic_load_explicit(&hck_tab, memory_order_acquire)))
		pthread_once(&hck_once, hck_init);
	const hck **cc = &p->ckc[h >> 52];
	_Atomic(const hck *) *tab;

	if (*cc && (*cc)->h == h && (*cc)->n == n && (*cc)->esz == esz && !memcmp((*cc)->d, a, bytes))
		return *cc;
	tab = atomic_load_explicit(&hck_tab, memory_order_relaxed);

	for (uint32_t i = (uint32_t)(h >> (64 - HCK_BITS)), k = 0; k < 64; i = (i + 1) & ((1u << HCK_BITS) - 1), k++) {
		const hck *e = atomic_load_explicit(&tab[i], memory_order_acquire);

		if (!e) {
			if (!mine) {
				mine = hsalloc(p, sizeof(hck) + bytes + 16);
				mine->h = h;
				mine->n = n;
				mine->esz = esz;
				memcpy(mine->d, a, bytes);
				memset(mine->d + bytes, 0, 16);
			}
			if (atomic_compare_exchange_strong_explicit(&tab[i], &e, mine, memory_order_acq_rel,
								    memory_order_acquire))
				return *cc = mine;
		}
		if (e->h == h && e->n == n && e->esz == esz && !memcmp(e->d, a, bytes))
			return *cc = e;	/* (a chunk of ours that lost a race stays unused) */
	}
	if (!mine) {
		mine = hsalloc(p, sizeof(hck) + bytes + 16);
		mine->h = h;
		mine->n = n;
		mine->esz = esz;
		memcpy(mine->d, a, bytes);
		memset(mine->d + bytes, 0, 16);
	}
	return mine;	/* table crowded here: not shared */
}

/* n bytes equal to s, NUL and 15 more zero bytes after them, stored once */
VPRIV const void *hc_blob(pp *p, const void *s, size_t n)
{
	if (n >= 0xffffffffu) {
		char *d = hsalloc(p, n + 16);

		memcpy(d, s, n);
		memset(d + n, 0, 16);
		return d;
	}
	return hck_get(p, s, (uint32_t)n, 1)->d;
}

/*
 * Elements whose first 4 bytes are an id, cut into shared chunks after
 * every id whose hash has its low 5 bits clear (~32 per chunk, at most 256).
 */
VPRIV hvec hc_vec(pp *p, const void *a, uint32_t n, uint32_t esz)
{
	const char *b = a;
	const hck **c, **tmp;
	uint32_t nc = 0, st = 0;
	hvec v = { NULL, 0, n };

	if (!n)
		return v;
	tmp = varena_alloc(&p->tmp, (size_t)n * sizeof(*tmp));
	for (uint32_t i = 0; i < n; i++) {
		uint32_t id;

		memcpy(&id, b + (size_t)i * esz, 4);
		if (((id * 0x9E3779B1u) >> 27) == 0 || i + 1 - st == 256 || i + 1 == n) {
			/* a chunk this TU met lately, found by its ids before any hashing */
			uint32_t m = i + 1 - st, id0;
			const char *q = b + (size_t)st * esz;
			const hck **vc;

			memcpy(&id0, q, 4);
			vc = &p->vkc[(((uint64_t)id0 * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)id * 0xc2b2ae3d27d4eb4full) ^
				      ((uint64_t)m << 8 | esz)) * 0xff51afd7ed558ccdull >> 52];
			if (!*vc || (*vc)->n != m || (*vc)->esz != esz || memcmp((*vc)->d, q, (size_t)m * esz))
				*vc = hck_get(p, q, m, esz);
			tmp[nc++] = *vc;
			st = i + 1;
		}
	}
	c = hsalloc(p, nc * sizeof(*c));
	memcpy(c, tmp, nc * sizeof(*c));
	v.c = c;
	v.nc = nc;
	return v;
}
