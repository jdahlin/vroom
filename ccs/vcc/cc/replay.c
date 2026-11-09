/*
 * vcc front end: declaration replay per region (RESEARCH-preprocessor.md
 * §6.9), step 1: record only.  VCC_DRREC=1 opens a recording at every
 * file-scope declaration boundary where a header-cache hit starts, and closes
 * it where the hit ends at a boundary.  In between it logs what a decl entry
 * would need: the names looked up outside the region (bound or not), the
 * types used from before it, pragmas and diagnostics, and, by comparing a copy
 * of the state at the start, every write to entities that existed before the
 * region.  Nothing is stored or replayed; vpp --batch prints the sums.
 */
#include <pthread.h>
#include <stdatomic.h>

#include "ccimpl.h"

struct drrec {
	bool open;
	uint64_t b;		/* delivered position of the region's first token */
	const void *he;		/* its header-cache entry */
	uint32_t nsym_bi;	/* syms below: builtins (cc_init) */
	/* counts at the start */
	uint32_t nsym0, ntag0, nty0, nnode0, nlist0, nipool0, nfpool0, nstr0, nattr0, nasm0, ntl0, ntlasm0;
	uint32_t nlzt0, nlzf0, nlzs0, nparams0, npack0;
	size_t heap0;
	int errors0, ndiag0, pperr0;
	/* what happened */
	bool weak, pack, main_read, pack_read;
	uint32_t other_pragmas;
	uint32_t r_bound, r_unbound, r_builtin, t_ext;
	/* the reads so far: (namespace << 31 | id) or (2 << 31 | type index), open addressing */
	uint64_t *hs;
	uint32_t hcap, hn;
	/* the state at the start: syms, tags, types, attrs before the region */
	sym *ssym;
	tag *stag;
	ctype *sty;
	attrs *sattr;
	uint32_t csym, ctag, cty, cattr;
	/* types: the region's interning table, the TU's while it is swapped out */
	uint32_t *rt, rcap, rn;
	uint32_t *real, realcap;
};

static int drrec_on = -1;
static pthread_mutex_t he_mu = PTHREAD_MUTEX_INITIALIZER;
static const void **he_set;
static uint32_t he_cap, he_n;

enum { DS_OPEN, DS_CLOSED, DS_ABANDON_END, DS_ABANDON_LOOK, DS_ABANDON_RESTORE,
       DS_DIAG, DS_PRAGMA, DS_MAIN, DS_PURE, DS_PURE_PLAIN, DS_PURE_WRITES, DS_PURE_LAZY, DS_PACK_READ, DS_N };
static _Atomic uint64_t st_n[DS_N], st_tok[DS_N];
static _Atomic uint64_t st_bound, st_unbound, st_builtin, st_text, st_newty, st_syms, st_tags, st_nodes, st_bytes;
static _Atomic uint64_t st_wsym, st_wtag, st_wty, st_wattr, st_wflags_only, st_wsym_ty, st_wsym_attr, st_wsym_other;
static _Atomic uint64_t st_wtag_complete, st_wtag_other, st_lzf, st_flagbits;
static const char *const sfield[] = { "loc", "kind", "sc", "flags", "ty", "align", "val", "body", "asmname", "attrs",
	"shadow", "depth", "next", "ovl", "plb", "ple", "psyms" };
static _Atomic uint64_t st_sfield[17];

static void sym_fields(const sym *a, const sym *b)
{
#define F(i, x) if (a->x != b->x) atomic_fetch_add_explicit(&st_sfield[i], 1, memory_order_relaxed)
	F(0, loc); F(1, kind); F(2, sc); F(3, flags); F(4, ty); F(5, align); F(6, val); F(7, body); F(8, asmname); F(9, attrs);
	F(10, shadow); F(11, depth); F(12, next); F(13, ovl); F(14, plb); F(15, ple); F(16, psyms);
#undef F
}

/* ---------------- the read set ---------------- */

static bool rs_add(struct drrec *r, uint64_t k)
{
	uint32_t m;

	if (r->hn * 2 >= r->hcap) {
		uint32_t ncap = r->hcap ? r->hcap * 2 : 1024;
		uint64_t *nh = calloc(ncap, sizeof(uint64_t));

		for (uint32_t i = 0; i < r->hcap; i++)
			if (r->hs[i]) {
				uint32_t j = (uint32_t)(r->hs[i] * 0x9E3779B97F4A7C15ull >> 40) & (ncap - 1);

				while (nh[j])
					j = (j + 1) & (ncap - 1);
				nh[j] = r->hs[i];
			}
		free(r->hs);
		r->hs = nh, r->hcap = ncap;
	}
	m = r->hcap - 1;
	k++;	/* (0: empty) */
	for (uint32_t j = (uint32_t)(k * 0x9E3779B97F4A7C15ull >> 40) & m;; j = (j + 1) & m) {
		if (!r->hs[j]) {
			r->hs[j] = k, r->hn++;
			return true;
		}
		if (r->hs[j] == k)
			return false;
	}
}

/* a name lookup (ns 0: ordinary, 1: tag) gave s */
VPRIV __attribute__((noinline)) void dr_read(cc *c, int ns, uint32_t id, uint32_t s)
{
	struct drrec *r = c->dr;

	if (!r->open || (s && s >= r->nsym0))
		return;
	if (!rs_add(r, (uint64_t)ns << 31 | id))
		return;
	if (!s)
		r->r_unbound++;
	else if (s < r->nsym_bi)
		r->r_builtin++;
	else {
		r->r_bound++;
		if (pp_in_main_file(c->p, c->syms[s].loc))
			r->main_read = true;
	}
}

/*
 * Types.  While a region is recorded, c->tyhash is a table of its own, so
 * ty_intern reaches dr_intern exactly once per structural type the region
 * interns: the type log a decl entry needs (old types it used, new ones in
 * order).  The TU's table stays complete.  ty_ptr's cache ignores pointer
 * types from before the region (ptr_lo), so they come through here too.
 */
static void rt_insert(struct drrec *r, cc *c, uint32_t idx)
{
	uint32_t m;

	if ((r->rn + 1) * 2 > r->rcap) {
		uint32_t ncap = r->rcap * 2, *nh = calloc(ncap, sizeof(uint32_t));

		for (uint32_t j = 0; j < r->rcap; j++) {
			uint32_t x = r->rt[j], k;

			if (!x)
				continue;
			for (k = ty_hash(&c->ty[x]) & (ncap - 1); nh[k]; k = (k + 1) & (ncap - 1))
				;
			nh[k] = x;
		}
		free(r->rt);
		r->rt = nh, r->rcap = ncap;
		c->tyhash = r->rt, c->tyhcap = r->rcap;
	}
	m = r->rcap - 1;
	for (uint32_t k = ty_hash(&c->ty[idx]) & m;; k = (k + 1) & m)
		if (!r->rt[k]) {
			r->rt[k] = idx, r->rn++;
			return;
		}
}

VPRIV __attribute__((noinline)) uint32_t dr_intern(cc *c, const ctype *t)
{
	struct drrec *r = c->dr;
	uint32_t before = c->nty, i;

	c->tyhash = r->real, c->tyhcap = r->realcap, c->dr_on = 0;
	i = ty_intern(c, t);
	r->real = c->tyhash, r->realcap = c->tyhcap, c->dr_on = 1;
	c->tyhash = r->rt, c->tyhcap = r->rcap;
	if (i < before && i < r->nty0)
		r->t_ext++;
	rt_insert(r, c, i);
	return i;
}

/* recording is over (closed or abandoned): the TU's own table and cache back */
static void rec_stop(cc *c)
{
	struct drrec *r = c->dr;

	if (c->tyhash == r->rt)
		c->tyhash = r->real, c->tyhcap = r->realcap;
	c->ptr_lo = 1;
	c->dr_on = 0;
	r->open = false;
}

VPRIV __attribute__((noinline)) void dr_pragma(cc *c, int kind)
{
	struct drrec *r = c->dr;

	if (!r->open)
		return;
	if (kind == 1)
		r->weak = true;
	else if (kind == 2)
		r->pack = true;
	else
		r->other_pragmas++;
}

/* ---------------- opening and closing ---------------- */

static void *grow(void *p, uint32_t *cap, uint32_t need, size_t el)
{
	if (need <= *cap)
		return p;
	*cap = need + need / 2 + 1024;
	return realloc(p, (size_t)*cap * el);
}

static void rec_open(cc *c, uint64_t b, const void *he)
{
	struct drrec *r = c->dr;

	r->open = true;
	r->b = b, r->he = he;
	r->nsym0 = c->nsym, r->ntag0 = c->ntag, r->nty0 = c->nty, r->nnode0 = c->nnode, r->nlist0 = c->nlist;
	r->nipool0 = c->nipool, r->nfpool0 = c->nfpool, r->nstr0 = c->nstr, r->nattr0 = c->nattr, r->nasm0 = c->nasm;
	r->ntl0 = c->ntoplevel, r->ntlasm0 = c->ntlasm, r->nlzt0 = c->nlzt, r->nlzf0 = c->nlzf, r->nlzs0 = c->nlzs;
	r->nparams0 = c->nparams, r->npack0 = (uint32_t)c->npack;
	r->heap0 = (size_t)(c->hp - c->heap);
	r->errors0 = c->errors, r->ndiag0 = (int)c->ndiag, r->pperr0 = pp_errors(c->p);
	r->weak = r->pack = r->main_read = false;
	r->pack_read = c->npack && c->pack[c->npack - 1];
	r->other_pragmas = r->r_bound = r->r_unbound = r->r_builtin = r->t_ext = 0;
	if (r->hn) {
		memset(r->hs, 0, r->hcap * sizeof(uint64_t));
		r->hn = 0;
	}
	r->ssym = grow(r->ssym, &r->csym, c->nsym, sizeof(sym));
	memcpy(r->ssym, c->syms, c->nsym * sizeof(sym));
	r->stag = grow(r->stag, &r->ctag, c->ntag, sizeof(tag));
	memcpy(r->stag, c->tags, c->ntag * sizeof(tag));
	r->sty = grow(r->sty, &r->cty, c->nty, sizeof(ctype));
	memcpy(r->sty, c->ty, c->nty * sizeof(ctype));
	r->sattr = grow(r->sattr, &r->cattr, c->nattr, sizeof(attrs));
	memcpy(r->sattr, c->attr, c->nattr * sizeof(attrs));
	if (!r->rt)
		r->rt = calloc(r->rcap = 1024, sizeof(uint32_t));
	else if (r->rn)
		memset(r->rt, 0, r->rcap * sizeof(uint32_t));
	r->rn = 0;
	r->real = c->tyhash, r->realcap = c->tyhcap;
	c->tyhash = r->rt, c->tyhcap = r->rcap;
	c->ptr_lo = c->nty;
	c->dr_on = 1;
}

static void count(int k, uint64_t tok)
{
	atomic_fetch_add_explicit(&st_n[k], 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&st_tok[k], tok, memory_order_relaxed);
}

static void rec_abandon(cc *c, int why, uint64_t tok)
{
	rec_stop(c);
	count(why, tok);
}

static void he_note(const void *he)
{
	pthread_mutex_lock(&he_mu);
	if (he_n * 2 >= he_cap) {
		uint32_t ncap = he_cap ? he_cap * 2 : 4096;
		const void **nh = calloc(ncap, sizeof(void *));

		for (uint32_t i = 0; i < he_cap; i++)
			if (he_set[i]) {
				uint32_t j = (uint32_t)((uintptr_t)he_set[i] * 0x9E3779B97F4A7C15ull >> 40) & (ncap - 1);

				while (nh[j])
					j = (j + 1) & (ncap - 1);
				nh[j] = he_set[i];
			}
		free(he_set);
		he_set = nh, he_cap = ncap;
	}
	for (uint32_t j = (uint32_t)((uintptr_t)he * 0x9E3779B97F4A7C15ull >> 40) & (he_cap - 1);;
	     j = (j + 1) & (he_cap - 1)) {
		if (!he_set[j]) {
			he_set[j] = he, he_n++;
			break;
		}
		if (he_set[j] == he)
			break;
	}
	pthread_mutex_unlock(&he_mu);
}

#define ADD(x, v) atomic_fetch_add_explicit(&(x), (v), memory_order_relaxed)

static void rec_close(cc *c, uint64_t e)
{
	struct drrec *r = c->dr;
	uint64_t tok = e - r->b, bytes;
	uint32_t wsym = 0, wtag = 0, wty = 0, wattr = 0, flagbits = 0;
	bool writes, lazy = c->nlzf > r->nlzf0;

	rec_stop(c);
	count(DS_CLOSED, tok);
	he_note(r->he);
	if (c->errors != r->errors0 || (int)c->ndiag != r->ndiag0 || pp_errors(c->p) != r->pperr0) {
		count(DS_DIAG, tok);
		return;
	}
	if (r->weak || r->pack || (uint32_t)c->npack != r->npack0) {
		count(DS_PRAGMA, tok);
		return;
	}
	if (r->main_read) {
		count(DS_MAIN, tok);
		return;
	}
	count(DS_PURE, tok);
	if (r->pack_read)
		count(DS_PACK_READ, tok);
	/* writes to what existed before */
	for (uint32_t i = 0; i < r->nsym0; i++) {
		const sym *a = &r->ssym[i], *b = &c->syms[i];

		if (!memcmp(a, b, sizeof(sym)))
			continue;
		wsym++;
		sym_fields(a, b);
		flagbits |= a->flags ^ b->flags;
		{
			sym x = *a;

			x.flags = b->flags;
			if (!memcmp(&x, b, sizeof(sym)))
				ADD(st_wflags_only, 1);
			else if (a->ty != b->ty)
				ADD(st_wsym_ty, 1);
			else if (a->attrs != b->attrs || a->align != b->align || a->asmname != b->asmname)
				ADD(st_wsym_attr, 1);
			else
				ADD(st_wsym_other, 1);
		}
	}
	for (uint32_t i = 0; i < r->ntag0; i++)
		if (memcmp(&r->stag[i], &c->tags[i], sizeof(tag))) {
			wtag++;
			if (!(r->stag[i].flags & RF_COMPLETE) && (c->tags[i].flags & RF_COMPLETE))
				ADD(st_wtag_complete, 1);
			else
				ADD(st_wtag_other, 1);
		}
	for (uint32_t i = 0; i < r->nty0; i++)
		wty += memcmp(&r->sty[i], &c->ty[i], sizeof(ctype)) != 0;
	for (uint32_t i = 0; i < r->nattr0 && i < c->nattr; i++)
		wattr += memcmp(&r->sattr[i], &c->attr[i], sizeof(attrs)) != 0;
	writes = wsym || wtag || wty || wattr;
	count(!writes && !lazy ? DS_PURE_PLAIN : writes ? DS_PURE_WRITES : DS_PURE_LAZY, tok);
	bytes = (uint64_t)(c->nsym - r->nsym0) * sizeof(sym) + (uint64_t)(c->ntag - r->ntag0) * sizeof(tag) +
		(uint64_t)(c->nnode - r->nnode0) * (sizeof(node) + sizeof(pp_span)) +
		(uint64_t)(c->nlist - r->nlist0) * 4 + (uint64_t)(c->nipool - r->nipool0) * 16 +
		(uint64_t)(c->nfpool - r->nfpool0) * 8 + (uint64_t)(c->nstr - r->nstr0) * sizeof(cstr) +
		(uint64_t)(c->nattr - r->nattr0) * sizeof(attrs) + (uint64_t)(c->nasm - r->nasm0) * sizeof(casm) +
		(uint64_t)(c->ntoplevel - r->ntl0) * 4 + (uint64_t)(c->nparams - r->nparams0) * 4 +
		(uint64_t)(c->nlzt - r->nlzt0) * sizeof(lztok) + (uint64_t)(c->nlzf - r->nlzf0) * sizeof(lzfn) +
		(c->nlzs - r->nlzs0) + ((size_t)(c->hp - c->heap) - r->heap0) +
		(uint64_t)(c->nty - r->nty0 + r->t_ext) * sizeof(ctype) + (uint64_t)(r->r_bound + r->r_unbound) * 16;
	ADD(st_bound, r->r_bound);
	ADD(st_unbound, r->r_unbound);
	ADD(st_builtin, r->r_builtin);
	ADD(st_text, r->t_ext);
	ADD(st_newty, c->nty - r->nty0);
	ADD(st_syms, c->nsym - r->nsym0);
	ADD(st_tags, c->ntag - r->ntag0);
	ADD(st_nodes, c->nnode - r->nnode0);
	ADD(st_bytes, bytes);
	ADD(st_wsym, wsym);
	ADD(st_wtag, wtag);
	ADD(st_wty, wty);
	ADD(st_wattr, wattr);
	ADD(st_lzf, c->nlzf - r->nlzf0);
	atomic_fetch_or_explicit(&st_flagbits, flagbits, memory_order_relaxed);
}

/* at a file-scope declaration boundary (translation_unit), with VCC_DRREC */
VPRIV void dr_boundary(cc *c)
{
	struct drrec *r = c->dr;
	uint64_t pos, b;
	const void *he;

	if (c->ntk != 1) {
		if (r->open)
			rec_abandon(c, DS_ABANDON_LOOK, 0);
		return;
	}
	pos = pp_ntok(c->p) - 1;
	if (r->open) {
		uint32_t n;
		const pp_hitreg *h = pp_hit_regions(c->p, &n);
		bool done = false;

		for (uint32_t i = n; i-- > 0 && h[i].b >= r->b;)
			if (h[i].b == r->b) {
				if (h[i].e == pos)
					rec_close(c, pos);
				else
					rec_abandon(c, DS_ABANDON_END, h[i].e - r->b);
				done = true;
				break;
			}
		if (!done) {
			if (pp_open_hit(c->p, &b, &he) && b == r->b)
				return;	/* still in it */
			rec_abandon(c, DS_ABANDON_END, 0);
		}
	}
	if (pp_open_hit(c->p, &b, &he) && b == pos) {
		count(DS_OPEN, 0);
		rec_open(c, b, he);
	}
}

/* ---------------- per TU ---------------- */

VPRIV void dr_begin(cc *c)
{
	if (drrec_on < 0)
		drrec_on = getenv("VCC_DRREC") != NULL;
	c->dr = NULL;
	c->dr_on = 0;
	if (!drrec_on)
		return;
	c->dr = calloc(1, sizeof(struct drrec));
	c->dr->nsym_bi = c->nsym;
}

VPRIV void dr_end(cc *c)
{
	struct drrec *r = c->dr;

	if (!r)
		return;
	if (r->open) {
		rec_stop(c);
		count(DS_ABANDON_END, 0);
	}
	free(r->rt);
	free(r->hs);
	free(r->ssym);
	free(r->stag);
	free(r->sty);
	free(r->sattr);
	free(r);
	c->dr = NULL;
	c->dr_on = 0;
}

/* a declaration-cache restore replaced the state: a recording in progress is void */
void dr_restored(cc *c)
{
	if (c->dr && c->dr->open) {
		c->dr->open = false;	/* (the snapshot's type table is in place: no swap back) */
		c->ptr_lo = 1;
		c->dr_on = 0;
		count(DS_ABANDON_RESTORE, 0);
	}
}

#define M(x) ((double)atomic_load(&(x)) / 1e6)
#define K(x) ((unsigned long long)atomic_load(&(x)))

void cc_dr_stats(char *buf, size_t n)
{
	uint64_t np = atomic_load(&st_n[DS_PURE]);
	int k;

	if (drrec_on <= 0) {
		*buf = 0;
		return;
	}
	k = snprintf(buf, n,
		     "drrec: opened %llu; closed %llu (%.1f M tok, %u entries); abandoned: end %llu (%.1f M), lookahead %llu, restore %llu\n"
		     "  closed: diag %llu (%.1f M), pragma pack/weak %llu (%.1f M), main-file read %llu (%.1f M), pure %llu (%.1f M; %llu read a pack state)\n"
		     "  pure: no writes no lazy %llu (%.1f M), writes %llu (%.1f M), lazy only %llu (%.1f M)\n",
		     K(st_n[DS_OPEN]), K(st_n[DS_CLOSED]), M(st_tok[DS_CLOSED]), he_n, K(st_n[DS_ABANDON_END]),
		     M(st_tok[DS_ABANDON_END]), K(st_n[DS_ABANDON_LOOK]), K(st_n[DS_ABANDON_RESTORE]),
		     K(st_n[DS_DIAG]), M(st_tok[DS_DIAG]), K(st_n[DS_PRAGMA]), M(st_tok[DS_PRAGMA]), K(st_n[DS_MAIN]),
		     M(st_tok[DS_MAIN]), np, M(st_tok[DS_PURE]), K(st_n[DS_PACK_READ]),
		     K(st_n[DS_PURE_PLAIN]), M(st_tok[DS_PURE_PLAIN]), K(st_n[DS_PURE_WRITES]), M(st_tok[DS_PURE_WRITES]),
		     K(st_n[DS_PURE_LAZY]), M(st_tok[DS_PURE_LAZY]));
	if (k < 0 || (size_t)k >= n || !np)
		return;
	snprintf(buf + k, n - (size_t)k,
		 "  per pure region: reads %.1f bound + %.1f unbound (+%.1f builtin), %.1f old types used, %.1f new types,\n"
		 "    %.1f syms, %.1f tags, %.1f nodes, %.1f lazy bodies, %.0f bytes (%.1f per token)\n"
		 "  writes to older entities: syms %llu (flags only %llu, type %llu, attrs %llu, other %llu; flag bits 0x%llx),\n"
		 "    tags %llu (completed %llu, other %llu), types %llu, attrs %llu",
		 (double)K(st_bound) / np, (double)K(st_unbound) / np, (double)K(st_builtin) / np, (double)K(st_text) / np,
		 (double)K(st_newty) / np, (double)K(st_syms) / np, (double)K(st_tags) / np, (double)K(st_nodes) / np,
		 (double)K(st_lzf) / np, (double)K(st_bytes) / np, (double)K(st_bytes) / (double)atomic_load(&st_tok[DS_PURE]),
		 K(st_wsym), K(st_wflags_only), K(st_wsym_ty), K(st_wsym_attr), K(st_wsym_other), K(st_flagbits),
		 K(st_wtag), K(st_wtag_complete), K(st_wtag_other), K(st_wty), K(st_wattr));
	k = (int)strlen(buf);
	for (int i = 0; i < 17 && (size_t)k + 32 < n; i++)
		if (atomic_load(&st_sfield[i]))
			k += snprintf(buf + k, n - (size_t)k, "%s %s %llu", i ? "" : "\n  sym fields written:", sfield[i],
				      K(st_sfield[i]));
}
