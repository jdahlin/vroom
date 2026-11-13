/* vcc preprocessor: header cache: may an #include be a region, and does an entry's recorded input match this TU */
#include "ppimpl.h"

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

/* the search directories e depended on (with its children) are not this TU's: its own lookups must give the same answers */
static inline bool dirs_differ(const pp *p, const hent *e)
{
	return (int)e->ndirs > p->o->ndirs || p->psig[e->ndirs] != e->psig;
}

/* e's own reads (guards first) hold in this state, except excluded ids */
static bool own_reads_ok(pp *p, const hent *e, const hexf *xs, int nx)
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
	return !e->nlk || !dirs_differ(p, e) || hc_looks_ok(p, e);
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
	if (e->nlk && dirs_differ(p, e) && !hc_looks_ok(p, e))
		return false;
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
