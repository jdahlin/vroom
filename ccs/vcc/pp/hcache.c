/* vcc preprocessor: header cache: types, the shared table, regions */
#include "ppimpl.h"
#include <arm_acle.h>

hstat_t hstat;

#define HKEY_BITS 16
static _Atomic(hkey *) hkey_tab[1u << HKEY_BITS];
size_t hc_mem_max;

/*
 * Signatures.  The base signature is every option that decides what a
 * header does except the macros and the search path; the search path is an
 * input of a region only as far as its lookups went: an entry records how
 * many leading search directories its #include / __has_include lookups
 * depended on (up to the one a file was found in; all of them for a
 * lookup that failed), and matches TUs whose search path starts with the
 * same directories.  (kbuild adds -I for each source directory at the end:
 * that does not split the cache.)
 */
static pthread_mutex_t sig_mu = PTHREAD_MUTEX_INITIALIZER;
static char *sig_str[1 << 16];
static size_t sig_len[1 << 16];
static uint32_t nsig;

static uint32_t sig_intern(char *b, size_t n)
{
	uint32_t r = HNONE;

	pthread_mutex_lock(&sig_mu);
	for (uint32_t i = 0; i < nsig; i++)
		if (sig_len[i] == n && !memcmp(sig_str[i], b, n)) {
			r = i;
			break;
		}
	if (r == HNONE && nsig < (1 << 16)) {
		char *c = malloc(n ? n : 1);

		memcpy(c, b, n);
		sig_str[nsig] = c;
		sig_len[nsig] = n;
		r = nsig++;
	}
	pthread_mutex_unlock(&sig_mu);
	return r;
}

/* base signature into *sig, prefix ids into psig[0..ndirs]; false if too long */
VPRIV bool sig_of(const pp_opts *o, uint32_t *sig, uint32_t *psig)
{
	char *b = malloc(1 << 17);
	size_t n = 0;
	bool ok = true;

#define SIGS(x) do { const char *s_ = (x) ? (x) : ""; size_t l_ = strlen(s_); \
	if (n + l_ + 8 < (1 << 17)) { memcpy(b + n, s_, l_); n += l_; b[n++] = 0; } else ok = false; } while (0)
	n += (size_t)snprintf(b, 64, "%d %d %d %d %d %d", o->nfwdirs, o->unsigned_char, o->asm_mode, o->target,
			      o->nwargs, o->error_limit) + 1;
	for (int i = 0; i < o->nwargs; i++)	/* -W/-Werror decide which replayed diagnostics are errors */
		SIGS(o->wargs[i]);
	for (int i = 0; i < o->nfwdirs; i++)
		SIGS(o->fwdirs[i]);
	SIGS(o->cwd);
	SIGS(o->prefix_from);
	SIGS(o->prefix_to);
	*sig = sig_intern(b, n);
	n = 0;
	psig[0] = sig_intern(b, 0);
	for (int i = 0; i < o->ndirs && ok; i++) {
		/* each directory with whether it is a quote-only and a system directory */
		b[n++] = (char)('0' + (i < o->nquote) + 2 * (o->nsys && i >= o->nsys));
		SIGS(o->dirs[i]);
		psig[i + 1] = sig_intern(b, n);
	}
#undef SIGS
	free(b);
	return ok && *sig != HNONE;
}

VPRIV hkey *hkey_get(uint32_t sig, const vlex_file *f, int32_t sdir, uint32_t nfile, const char *spath, uint8_t sys,
		     bool make)
{
	size_t sl = strlen(spath);
	uint64_t h = ((uint64_t)(uintptr_t)f * 0x9E3779B97F4A7C15ull) ^
		     ((uint64_t)sig << 40 | (uint64_t)(uint32_t)sdir << 16 | (uint64_t)sys << 15 | nfile);
	hkey *mine = NULL;

	h = (h ^ vintern_hash(spath, sl)) * 0xff51afd7ed558ccdull;
	h ^= h >> 29;
	for (uint32_t i = (uint32_t)(h >> (64 - HKEY_BITS)), k = 0; k < (1u << HKEY_BITS);
	     i = (i + 1) & ((1u << HKEY_BITS) - 1), k++) {
		hkey *e = atomic_load_explicit(&hkey_tab[i], memory_order_acquire);

		if (!e) {
			if (!make)
				return NULL;
			if (!mine) {
				char *sp = salloc(sl + 1);

				memcpy(sp, spath, sl + 1);
				mine = salloc(sizeof(*mine));
				mine->hash = h;
				mine->f = f;
				mine->spath = sp;
				mine->sig = sig;
				mine->nfile = nfile;
				mine->sdir = sdir;
				mine->sys = sys;
			}
			if (atomic_compare_exchange_strong_explicit(&hkey_tab[i], &e, mine, memory_order_acq_rel,
								    memory_order_acquire))
				return mine;
		}
		if (e->hash == h && e->f == f && e->sig == sig && e->sdir == sdir && e->nfile == nfile && e->sys == sys &&
		    !strcmp(e->spath, spath))
			return e;
	}
	return NULL;
}

VPRIV int once_index(const pp *p, const vlex_file *f)
{
	for (uint32_t i = 0; i < p->nonce; i++)
		if (p->once[i] == f)
			return (int)i;
	return -1;
}

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
	tmp = alloc(&p->tmp, (size_t)n * sizeof(*tmp));
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

/* can an #include here be a region? */
VPRIV bool hc_top(const pp *p)
{
	return p->hc && p->sig != HNONE && p->rec.n < HC_MAXLEV && p->depth == p->nfile && !p->svn && !p->pend &&
	       !p->preexp && !p->collecting && !p->in_dir;
}

/* exclusions along a path of child entries: reads of these ids are not inputs */
typedef struct hexf {
	const uint32_t *ex;
	uint32_t n;
} hexf;

static bool hex_has(const hexf *xs, int nx, uint32_t id)
{
	for (int f = 0; f < nx; f++) {
		uint32_t lo = 0, hi = xs[f].n;

		while (lo < hi) {
			uint32_t m = (lo + hi) / 2;

			if (xs[f].ex[m] < id)
				lo = m + 1;
			else
				hi = m;
		}
		if (lo < xs[f].n && xs[f].ex[lo] == id)
			return true;
	}
	return false;
}

/* e's own reads (guards first) hold in this state, except excluded ids */
static bool own_reads_ok(const pp *p, const hent *e, const hexf *xs, int nx)
{
	HV_FOR(e->rg, hread, r)
		if ((mget(p, r->id) != 0) != (r->v != 0) && !(nx && hex_has(xs, nx, r->id)))
			return false;
	HV_FOR(e->ru, uint32_t, r)
		if (mget(p, *r) && !(nx && hex_has(xs, nx, *r)))
			return false;
	HV_FOR(e->rv, hread, r) {
		uintptr_t c = mget(p, r->id) & ~(uintptr_t)1, v = r->v;

		if ((v == HDEF ? !c : c != v) && !(nx && hex_has(xs, nx, r->id)))
			return false;
	}
	return true;
}

static bool tree_ok(pp *p, const hent *e, hexf *xs, int nx);

/* e's child entries' reads hold (a verdict without exclusions is memoised per hc_try) */
static bool kids_ok(pp *p, const hent *e, hexf *xs, int nx)
{
	for (uint32_t i = 0; i < e->nkid; i++) {
		const hkid *k = &e->kid[i];
		bool ok;

		if (!nx && !k->nex) {
			uint32_t h = (uint32_t)((((uintptr_t)k->e >> 4) * 0x9E3779B97F4A7C15ull) >> 52);

			if (p->hm[h].e == k->e && p->hm[h].gen == p->hgen) {
				ok = p->hm[h].ok;
			} else {
				ok = tree_ok(p, k->e, xs, 0);
				p->hm[h].e = k->e, p->hm[h].gen = p->hgen, p->hm[h].ok = ok;
			}
		} else {
			int m = nx;

			if (k->nex && nx < HC_MAXLEV)
				xs[m++] = (hexf){ k->ex, k->nex };
			ok = tree_ok(p, k->e, xs, m);
		}
		if (!ok)
			return false;
	}
	return true;
}

static bool tree_ok(pp *p, const hent *e, hexf *xs, int nx)
{
	return own_reads_ok(p, e, xs, nx) && kids_ok(p, e, xs, nx);
}

VPRIV bool hc_valid(pp *p, const hent *e)
{
	hexf xs[HC_MAXLEV + 1];

	if (e->level >= 0 && e->level != p->nfile)
		return false;
	if ((int)e->ndirs > p->o->ndirs || p->psig[e->ndirs] != e->psig)
		return false;
	HV_FOR(e->rg, hread, r)
		if ((mget(p, r->id) != 0) != (r->v != 0))
			return false;
	for (uint32_t i = 0; i < e->nord; i++)
		if ((once_index(p, e->ord[i].f) >= 0) != (bool)e->ord[i].present)
			return false;
	HV_FOR(e->ru, uint32_t, r)
		if (mget(p, *r))
			return false;
	HV_FOR(e->rv, hread, r) {
		uintptr_t c = mget(p, r->id) & ~(uintptr_t)1, v = r->v;

		if (v == HDEF ? !c : c != v)
			return false;
	}
	if (!kids_ok(p, e, xs, 0))
		return false;	/* (before the costly counter-name lookups) */
	for (uint32_t i = 0; i < e->ncrd; i++) {
		const hcread *r = &e->crd[i];
		char b[4096 + 40];
		int dl = utoa(b + r->off, p->counter + r->rel);
		size_t n = r->off + (size_t)dl + (r->len - r->off - r->dlen);
		uint32_t id;

		memcpy(b, r->s, r->off);
		memcpy(b + r->off + dl, r->s + r->off + r->dlen, r->len - r->off - r->dlen);
		memset(b + n, 0, 16);
		id = vintern_find(b, n, vintern_hash(b, n));
		if (id && mget(p, id))
			return false;
	}
	return true;
}

/* an entry's text, children spliced in, __COUNTER__ values from base */
VPRIV void emit(pp *p, const hent *e, unsigned base)
{
	uint32_t pos = 0, ci = 0;

	for (uint32_t si = 0; si <= e->nseg; si++) {
		uint32_t to = si < e->nseg ? e->seg[si].at : (uint32_t)e->ntext;

		while (ci < e->nctr && e->ctr[ci].off < to) {
			const hctr *c = &e->ctr[ci++];

			out_put(p, e->text + pos, c->off - pos);
			out_reserve(p, 24);
			p->nout += (size_t)utoa(p->out + p->nout, base + c->rel);
			pos = c->off + c->len;
		}
		out_put(p, e->text + pos, to - pos);
		pos = to;
		if (si < e->nseg)
			emit(p, e->seg[si].child, base + e->seg[si].cdelta);
	}
}
