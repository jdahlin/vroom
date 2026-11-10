/*
 * Declaration cache: the front end's state after a TU's leading run of
 * top-level headers, kept in memory and shared by the TUs that start the same
 * way (an in-memory chained PCH).
 *
 * The preprocessor reports top-level regions (pp_top_events): headers
 * included from the main file, each identified by its header-cache entry, so
 * that the same entry means the same tokens.  A trie keyed by (entry,
 * __COUNTER__ at its start) holds, per node, how many TUs went through it and,
 * from the second on, a snapshot of the parser state at the declaration
 * boundary right after it.  The state is a function of the token stream, so a
 * TU whose stream starts with the same regions can take the snapshot instead
 * of parsing them: the preprocessor still applies each region (macros,
 * files, counter) and only its token delivery is dropped (pp_skip_top).
 *
 * A snapshot is the used part of every per-TU array and of the heap, in 16 KB
 * pages shared with the nearest ancestor's snapshot where equal.  Heap
 * pointers are stored as offsets and locations relative to the first region's
 * location base (the main file comes first in location space, so the bases
 * differ between TUs), which makes snapshots of different TUs comparable.
 * Restoring is a copy and one pass over the location fields.
 *
 * VCC_NODC=1 turns it off; VCC_DC_MAX_MB caps the memory (default 256).
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "../cc/ccint.h"
#include "ccsnap.h"

#define DC_PAGE ((size_t)16384)
#define LOC_REL (1u << 31)	/* encoded locations: LOC_REL + (loc - base) */

enum { A_TY, A_PARAMS, A_TAGS, A_SYMS, A_NODES, A_LISTS, A_IPOOL, A_FPOOL, A_STRS, A_LABELS, A_ATTR, A_ASMS,
       A_SSTACK, A_TOPLEVEL, A_TLASM, A_SPAN, A_LZT, A_LZF, A_LZS, A_ATTRX, A_HEAP, A_N };

typedef struct dcarr {
	size_t n;		/* bytes */
	const char **pg;	/* ceil(n / DC_PAGE) pages */
} dcarr;

typedef struct dcsnap {
	cc c;			/* the scalar state; pointers into the heap as offsets */
	dcarr a[A_N];
} dcsnap;

typedef struct dcnode {
	const void *e;
	unsigned ctr;
	struct dcnode *_Atomic kids;
	struct dcnode *sib;
	struct dcnode *parent;
	_Atomic uint32_t visits;
	dcsnap *_Atomic snap;
} dcnode;


static struct dcroot {
	uint64_t sig;
	dcnode node;
	struct dcroot *next;
} *_Atomic roots;

static _Atomic uint64_t dc_mem, dc_max;
static _Atomic unsigned long st_snap, st_restore, st_regions;
static int dc_off;
static _Atomic uint64_t tok_skip;


/* ---------------- the arrays of a cc ---------------- */

static char *abase(const cc *c, int k, size_t *n)
{
	switch (k) {
	case A_TY: *n = c->nty * sizeof(ctype); return (char *)c->ty;
	case A_PARAMS: *n = c->nparams * sizeof(qty); return (char *)c->params;
	case A_TAGS: *n = c->ntag * sizeof(tag); return (char *)c->tags;
	case A_SYMS: *n = c->nsym * sizeof(sym); return (char *)c->syms;
	case A_NODES: *n = c->nnode * sizeof(node); return (char *)c->nodes;
	case A_LISTS: *n = c->nlist * sizeof(uint32_t); return (char *)c->lists;
	case A_IPOOL: *n = c->nipool * sizeof(i128); return (char *)c->ipool;
	case A_FPOOL: *n = c->nfpool * sizeof(double); return (char *)c->fpool;
	case A_STRS: *n = c->nstr * sizeof(cstr); return (char *)c->strs;
	case A_LABELS: *n = c->nlabel * sizeof(label); return (char *)c->labels;
	case A_ATTR: *n = c->nattr * sizeof(attrs); return (char *)c->attr;
	case A_ASMS: *n = c->nasm * sizeof(casm); return (char *)c->asms;
	case A_SSTACK: *n = c->nsstack * sizeof(uint32_t); return (char *)c->sstack;
	case A_TOPLEVEL: *n = c->ntoplevel * sizeof(uint32_t); return (char *)c->toplevel;
	case A_TLASM: *n = c->ntlasm * sizeof(uint32_t); return (char *)c->tlasm;
	case A_SPAN: *n = c->nnode * sizeof(pp_span); return (char *)c->span;
	case A_LZT: *n = c->nlzt * sizeof(lztok); return (char *)c->lzt;
	case A_LZF: *n = c->nlzf * sizeof(lzfn); return (char *)c->lzf;
	case A_LZS: *n = c->nlzs; return c->lzs;
	case A_ATTRX: *n = c->nattrx * sizeof(attrx); return (char *)c->attrx;
	default: *n = (size_t)(c->hp - c->heap); return c->heap;
	}
}

/* ---------------- encoding: heap pointers and locations ---------------- */

typedef struct coder {
	const char *heap;	/* the heap the pointers point into */
	size_t hn;
	char *to;		/* decoding: the heap they will point into */
	uint32_t base;		/* location base of the chain */
	bool bad;
} coder;

static void *enc_ptr(coder *k, const void *p)
{
	if (!p)
		return NULL;
	if ((const char *)p < k->heap || (const char *)p >= k->heap + k->hn) {
		k->bad = true;
		return NULL;
	}
	return (void *)((uintptr_t)((const char *)p - k->heap) + 1);
}

static void *dec_ptr(const coder *k, const void *p)
{
	return p ? k->to + ((uintptr_t)p - 1) : NULL;
}

static inline uint32_t enc_loc(coder *k, uint32_t l)
{
	if (l < k->base)
		return l;
	if (l - k->base >= LOC_REL)
		k->bad = true;
	return LOC_REL + (l - k->base);
}

static inline uint32_t dec_loc(const coder *k, uint32_t l)
{
	return l >= LOC_REL ? k->base + (l - LOC_REL) : l;
}

/* the location fields of an image (arrays at img[k], heap at img[A_HEAP], tags' f already coded) */
#define LOCS(img, c, FIX)                                                                       \
	do {                                                                                     \
		sym *sy_ = (sym *)(img)[A_SYMS];                                                 \
		node *nd_ = (node *)(img)[A_NODES];                                              \
		tag *tg_ = (tag *)(img)[A_TAGS];                                                 \
		label *lb_ = (label *)(img)[A_LABELS];                                           \
		pp_span *sp_ = (pp_span *)(img)[A_SPAN];                                         \
		lztok *lt_ = (lztok *)(img)[A_LZT];                                              \
		lzfn *lf_ = (lzfn *)(img)[A_LZF];                                                \
		attrx *ax_ = (attrx *)(img)[A_ATTRX];                                            \
		for (uint32_t i_ = 1; i_ < (c)->nattrx; i_++)                                    \
			ax_[i_].deprloc = FIX(ax_[i_].deprloc);                                  \
		for (uint32_t i_ = 0; i_ < (c)->nlzt; i_++)                                      \
			lt_[i_].loc = FIX(lt_[i_].loc);                                          \
		for (uint32_t i_ = 0; i_ < (c)->nlzf; i_++) {                                    \
			lf_[i_].loc = FIX(lf_[i_].loc);                                          \
			lf_[i_].plb = FIX(lf_[i_].plb);                                          \
			lf_[i_].ple = FIX(lf_[i_].ple);                                          \
			lf_[i_].z.base = FIX(lf_[i_].z.base);                                    \
			lf_[i_].z.rloc = FIX(lf_[i_].z.rloc);                                    \
		}                                                                                \
		for (uint32_t i_ = 0; i_ < (c)->nsym; i_++) {                                    \
			sy_[i_].loc = FIX(sy_[i_].loc);                                          \
			sy_[i_].plb = FIX(sy_[i_].plb);                                          \
			sy_[i_].ple = FIX(sy_[i_].ple);                                          \
		}                                                                                \
		for (uint32_t i_ = 0; i_ < (c)->nnode; i_++) {                                   \
			nd_[i_].loc = FIX(nd_[i_].loc);                                          \
			sp_[i_].b = FIX(sp_[i_].b);                                              \
			sp_[i_].e = FIX(sp_[i_].e);                                              \
		}                                                                                \
		for (uint32_t i_ = 0; i_ < (c)->nlabel; i_++)                                    \
			lb_[i_].loc = FIX(lb_[i_].loc);                                          \
		for (uint32_t i_ = 0; i_ < (c)->ntag; i_++) {                                    \
			tg_[i_].loc = FIX(tg_[i_].loc);                                          \
			if (tg_[i_].f) {                                                         \
				field *f_ = (field *)((img)[A_HEAP] + ((uintptr_t)tg_[i_].f - 1)); \
				for (uint32_t j_ = 0; j_ < tg_[i_].nfield; j_++)                 \
					f_[j_].loc = FIX(f_[j_].loc);                            \
			}                                                                        \
		}                                                                                \
	} while (0)

/* the pointer fields of the arrays (in place) */
static void code_ptrs(coder *k, char **img, const cc *c, bool enc)
{
	tag *tg = (tag *)img[A_TAGS];
	cstr *st = (cstr *)img[A_STRS];
	casm *as = (casm *)img[A_ASMS];

#define P(x) ((x) = enc ? enc_ptr(k, (x)) : dec_ptr(k, (x)))
	for (uint32_t i = 0; i < c->ntag; i++)
		P(tg[i].f);
	for (uint32_t i = 0; i < c->nstr; i++)
		P(st[i].s);
	for (uint32_t i = 0; i < c->nasm; i++) {
		P(as[i].ops);
		P(as[i].clob);
		P(as[i].labels);
	}
#undef P
}

/* the scalar state's heap pointers (in place) */
static void code_cc(coder *k, cc *s, bool enc)
{
#define P(x) ((x) = enc ? enc_ptr(k, (x)) : dec_ptr(k, (x)))
	P(s->tyhash);
	P(s->fnhash);	/* (dops: the TU keeps its own, see snap_restore) */
	for (uint32_t i = 0; i < NDIR; i++) {
		P(s->odir[i]);
		P(s->tdir[i]);
		P(s->ldir[i]);
	}
#undef P
	s->switch_cases = NULL;
}

/* ---------------- snapshots ---------------- */

static bool mem_take(size_t n)
{
	uint64_t m = atomic_fetch_add_explicit(&dc_mem, n, memory_order_relaxed);

	return m + n <= atomic_load_explicit(&dc_max, memory_order_relaxed);
}

static void *dalloc(size_t n)
{
	void *p = malloc(n);

	if (!p) {
		fputs("vcc: out of memory\n", stderr);
		abort();
	}
	return p;
}

static const dcsnap *ancestor_snap(const dcnode *n)
{
	for (n = n->parent; n; n = n->parent) {
		const dcsnap *s = atomic_load_explicit(&n->snap, memory_order_acquire);

		if (s)
			return s;
	}
	return NULL;
}

static _Thread_local char *img_buf;
static _Thread_local size_t img_cap;

static dcsnap *snap_take(const cc *c, const dcnode *n, uint32_t base)
{
	const dcsnap *par = ancestor_snap(n);
	size_t sz[A_N], tot = 0, off[A_N];
	char *img[A_N];
	coder k = { c->heap, (size_t)(c->hp - c->heap), NULL, base, false };
	dcsnap *s;

	for (int a = 0; a < A_N; a++) {
		abase(c, a, &sz[a]);
		off[a] = tot;
		tot += (sz[a] + 15) & ~(size_t)15;
	}
	if (tot > img_cap) {
		if (img_buf)
			munmap(img_buf, img_cap);
		img_cap = (tot + ((size_t)1 << 24)) & ~(((size_t)1 << 24) - 1);
		img_buf = mmap(NULL, img_cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (img_buf == MAP_FAILED) {
			img_buf = NULL, img_cap = 0;
			return NULL;
		}
	}
	for (int a = 0; a < A_N; a++) {
		size_t m;

		img[a] = img_buf + off[a];
		memcpy(img[a], abase(c, a, &m), sz[a]);
	}
	code_ptrs(&k, img, c, true);
#define ENC(l) enc_loc(&k, (l))
	LOCS(img, c, ENC);
#undef ENC
	if (k.bad)
		return NULL;
	s = dalloc(sizeof(*s));
	s->c = *c;
	code_cc(&k, &s->c, true);
	s->c.prevloc = enc_loc(&k, c->prevloc);
	if (k.bad) {
		free(s);
		return NULL;
	}
	{
		size_t own = sizeof(*s);

		for (int a = 0; a < A_N; a++) {
			size_t np = (sz[a] + DC_PAGE - 1) / DC_PAGE;

			s->a[a].n = sz[a];
			s->a[a].pg = dalloc((np ? np : 1) * sizeof(char *));
			own += np * sizeof(char *);
			for (size_t i = 0; i < np; i++) {
				size_t len = sz[a] - i * DC_PAGE < DC_PAGE ? sz[a] - i * DC_PAGE : DC_PAGE;
				const char *src = img[a] + i * DC_PAGE;
				char *pg;

				if (par && i * DC_PAGE + len <= par->a[a].n && !memcmp(par->a[a].pg[i], src, len)) {
					s->a[a].pg[i] = par->a[a].pg[i];
					continue;
				}
				pg = dalloc(DC_PAGE);
				memcpy(pg, src, len);
				s->a[a].pg[i] = pg;
				own += DC_PAGE;
			}
		}
		if (!mem_take(own))
			return s;	/* (published anyway: the cap stops the next ones) */
	}
	return s;
}

static void snap_restore(cc *c, const dcsnap *s, uint32_t base)
{
	cc keep = *c;
	coder k = { NULL, 0, c->heap, base, false };
	char *img[A_N];

	for (int a = 0; a < A_N; a++) {
		size_t n;
		char *to = a == A_HEAP ? c->heap : abase(c, a, &n);

		for (size_t i = 0; i * DC_PAGE < s->a[a].n; i++) {
			size_t len = s->a[a].n - i * DC_PAGE < DC_PAGE ? s->a[a].n - i * DC_PAGE : DC_PAGE;

			memcpy(to + i * DC_PAGE, s->a[a].pg[i], len);
		}
		img[a] = to;
	}
	memcpy(c, &s->c, sizeof(*c));
	/* this TU's own: the token state, the arrays, options */
	c->p = keep.p;
	c->o = keep.o;
	memcpy(c->bail, keep.bail, sizeof(c->bail));
	memcpy(c->tk, keep.tk, sizeof(c->tk));
	c->ntk = keep.ntk;
	c->ty = keep.ty, c->params = keep.params, c->tags = keep.tags, c->syms = keep.syms;
	c->nodes = keep.nodes, c->lists = keep.lists, c->ipool = keep.ipool, c->fpool = keep.fpool;
	c->strs = keep.strs, c->labels = keep.labels, c->attr = keep.attr, c->attrx = keep.attrx, c->asms = keep.asms;
	c->sstack = keep.sstack, c->toplevel = keep.toplevel, c->tlasm = keep.tlasm;
	c->span = keep.span;
	c->lzt = keep.lzt, c->lzf = keep.lzf, c->lzs = keep.lzs;
	/* malloc'd and per TU: the snapshot's would be shared (and realloc'ed) by every TU restoring it */
	c->cases = keep.cases, c->ncase = keep.ncase, c->capcase = keep.capcase, c->case_base = keep.case_base;
	c->heap = keep.heap, c->hend = keep.hend, c->ptrc = keep.ptrc;
	c->hp = c->heap + s->a[A_HEAP].n;
	c->dc = keep.dc;
	c->dops = keep.dops, c->ndops = keep.ndops;	/* (scratch in this TU's heap) */
	code_cc(&k, c, false);
	c->prevloc = dec_loc(&k, s->c.prevloc);
#define DEC(l) dec_loc(&k, (l))
	LOCS(img, c, DEC);	/* (first: the tags' f is still coded) */
#undef DEC
	code_ptrs(&k, img, c, false);
}

/* ---------------- the trie ---------------- */

/* every option before outfd can change what a header parses to (cc.h), so all
   of them key the trie, plus emit (code generation keeps more per decl) */
static uint64_t opt_sig(const cc_opts *o)
{
	const unsigned char *p = (const unsigned char *)o;
	uint64_t h = 1469598103934665603ull;

	for (size_t i = 0; i < offsetof(cc_opts, outfd); i++)
		h = (h ^ p[i]) * 1099511628211ull;
	return (h ^ (uint64_t)o->emit) * 1099511628211ull;
}

static dcnode *root_get(uint64_t sig)
{
	struct dcroot *r, *h;

	for (;;) {
		h = atomic_load_explicit(&roots, memory_order_acquire);
		for (r = h; r; r = r->next)
			if (r->sig == sig)
				return &r->node;
		r = calloc(1, sizeof(*r));
		r->sig = sig;
		r->next = h;
		if (atomic_compare_exchange_strong_explicit(&roots, &h, r, memory_order_acq_rel, memory_order_acquire))
			return &r->node;
		free(r);
	}
}

static dcnode *child(dcnode *n, const void *e, unsigned ctr, bool make)
{
	dcnode *h, *x;

	for (;;) {
		h = atomic_load_explicit(&n->kids, memory_order_acquire);
		for (x = h; x; x = x->sib)
			if (x->e == e && x->ctr == ctr)
				return x;
		if (!make)
			return NULL;
		x = calloc(1, sizeof(*x));
		x->e = e;
		x->ctr = ctr;
		x->parent = n;
		x->sib = h;
		if (atomic_compare_exchange_strong_explicit(&n->kids, &h, x, memory_order_acq_rel, memory_order_acquire))
			return x;
		free(x);
	}
}

/* ---------------- per TU ---------------- */

static void dc_env(void)
{
	const char *m = getenv("VCC_DC_MAX_MB");

	atomic_store(&dc_max, (uint64_t)(m ? atoll(m) : 256) << 20);
	dc_off = getenv("VCC_NODC") != NULL;
}

void dc_begin(cc *c)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	pthread_once(&once, dc_env);	/* worker threads race to the first TU */
	c->dc = NULL;
	if (dc_off)
		return;
	c->dc = calloc(1, sizeof(struct dcst));
	c->dc->node = root_get(opt_sig(c->o));
}

void dc_end(cc *c)
{
	if (c->dc)
		atomic_fetch_add_explicit(&tok_skip, pp_skipped(c->p), memory_order_relaxed);
	free(c->dc);
	c->dc = NULL;
}

void dc_materialize(cc *c)
{
	struct dcst *d = c->dc;
	const dcsnap *s = atomic_load_explicit(&d->target->snap, memory_order_acquire);

	d->stale = false;
	snap_restore(c, s, d->base);
	atomic_fetch_add_explicit(&st_restore, 1, memory_order_relaxed);
}

static void die(cc *c)
{
	if (c->dc->stale)
		dc_materialize(c);
	c->dc->dead = true;
}

/* no snapshot from here on: state the snapshots do not hold (#pragma weak) */
void dc_kill(cc *c)
{
	if (c->dc && !c->dc->dead)
		die(c);
}

void dc_boundary_slow(cc *c)
{
	struct dcst *d = c->dc;

	for (;;) {
		uint32_t n;
		const pp_topev *ev = pp_top_events(c->p, &n);
		uint64_t N = pp_ntok(c->p);

		if (!ev || c->ntk != 1) {
			die(c);
			return;
		}
		for (; d->ev < n; d->ev++) {
			const pp_topev *E = &ev[d->ev];

			if (!E->end) {
				if (d->is_open || E->pos != d->pos) {
					die(c);
					return;
				}
				if (!d->node->parent)
					d->base = E->base;
				d->open = *E;
				d->is_open = true;
				continue;
			}
			/* a region ended: right before tk[0], or the chain is over */
			if (!d->is_open || !E->e || E->pos + 1 != N) {
				die(c);
				return;
			}
			d->is_open = false;
			d->pos = E->pos;
			d->node = child(d->node, E->e, d->open.ctr, true);
			atomic_fetch_add_explicit(&d->node->visits, 1, memory_order_relaxed);
			if (!d->stale && atomic_load_explicit(&d->node->visits, memory_order_relaxed) >= 2 &&
			    !atomic_load_explicit(&d->node->snap, memory_order_acquire) && !c->errors && !c->ndiag &&
			    !pp_errors(c->p) && atomic_load_explicit(&dc_mem, memory_order_relaxed) <
			    atomic_load_explicit(&dc_max, memory_order_relaxed)) {
				dcsnap *s = snap_take(c, d->node, d->base), *z = NULL;

				if (s && atomic_compare_exchange_strong_explicit(&d->node->snap, &z, s,
										 memory_order_release, memory_order_relaxed))
					atomic_fetch_add_explicit(&st_snap, 1, memory_order_relaxed);
			}
		}
		/* tk[0] is the first token of a replayed region whose state is known: skip it */
		if (d->is_open && d->open.hit && d->open.pos + 1 == N) {
			dcnode *x = child(d->node, d->open.e, d->open.ctr, false);

			if (x && atomic_load_explicit(&x->snap, memory_order_acquire) && pp_skip_top(c->p)) {
				d->stale = true;
				d->target = x;
				atomic_fetch_add_explicit(&st_regions, 1, memory_order_relaxed);
				cc_fetch(c, &c->tk[0]);	/* (a pragma restores first) */
				continue;
			}
		}
		if (d->stale)
			dc_materialize(c);
		return;
	}
}

void dc_stats(char *buf, size_t n)
{
	snprintf(buf, n, "decl cache: %lu restores, %lu regions (%.1f M tokens) skipped, %lu snapshots, %.1f MB",
		 atomic_load(&st_restore), atomic_load(&st_regions), (double)atomic_load(&tok_skip) / 1e6,
		 atomic_load(&st_snap), (double)atomic_load(&dc_mem) / (1 << 20));
}
