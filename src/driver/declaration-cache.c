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
 * files, counter) and only its token delivery is dropped (pp_top_region_skip).
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

#include "frontend/c/ccint.h"
#include "driver/declaration-cache.h"

#define DRIVER_SNAPSHOT_PAGE_SIZE ((size_t)16384)
#define DRIVER_SNAPSHOT_LOCATION_RELATIVE (1u << 31)	/* encoded locations: DRIVER_SNAPSHOT_LOCATION_RELATIVE + (loc - base) */

enum { DRIVER_SNAPSHOT_ARRAY_TYPES, DRIVER_SNAPSHOT_ARRAY_PARAMETERS, DRIVER_SNAPSHOT_ARRAY_TAGS, DRIVER_SNAPSHOT_ARRAY_SYMBOLS, DRIVER_SNAPSHOT_ARRAY_NODES, DRIVER_SNAPSHOT_ARRAY_LISTS, DRIVER_SNAPSHOT_ARRAY_INTEGER_POOL, DRIVER_SNAPSHOT_ARRAY_FLOAT_POOL, DRIVER_SNAPSHOT_ARRAY_STRINGS, DRIVER_SNAPSHOT_ARRAY_LABELS, DRIVER_SNAPSHOT_ARRAY_ATTRIBUTES, DRIVER_SNAPSHOT_ARRAY_ASM_STATEMENTS,
       DRIVER_SNAPSHOT_ARRAY_SCOPE_STACK, DRIVER_SNAPSHOT_ARRAY_TOP_LEVEL, DRIVER_SNAPSHOT_ARRAY_TOP_LEVEL_ASM, DRIVER_SNAPSHOT_ARRAY_SPANS, DRIVER_SNAPSHOT_ARRAY_LAZY_TYPES, DRIVER_SNAPSHOT_ARRAY_LAZY_FUNCTIONS, DRIVER_SNAPSHOT_ARRAY_LAZY_SYMBOLS, DRIVER_SNAPSHOT_ARRAY_PARENTHESES, DRIVER_SNAPSHOT_ARRAY_HEAP, DRIVER_SNAPSHOT_ARRAY_COUNT };

typedef struct driver_snapshot_array {
	size_t bytes;		/* bytes */
	const char **pages;	/* ceil(n / DRIVER_SNAPSHOT_PAGE_SIZE) pages */
} driver_snapshot_array;

typedef struct driver_snapshot {
	cc state;			/* the scalar state; pointers into the heap as offsets */
	driver_snapshot_array arrays[DRIVER_SNAPSHOT_ARRAY_COUNT];
} driver_snapshot;

typedef struct driver_snapshot_node {
	const void *element;
	unsigned counter;
	struct driver_snapshot_node *_Atomic children;
	struct driver_snapshot_node *sibling;
	struct driver_snapshot_node *parent;
	_Atomic uint32_t visits;
	driver_snapshot *_Atomic snapshot;
} driver_snapshot_node;


static struct driver_snapshot_root {
	uint64_t signature;
	driver_snapshot_node node;
	struct driver_snapshot_root *next;
} *_Atomic driver_snapshot_roots;

static _Atomic uint64_t driver_snapshot_memory_used, driver_snapshot_memory_limit;
static _Atomic unsigned long driver_snapshot_take_count, driver_snapshot_restore_count, driver_snapshot_region_count;
static int driver_snapshot_disabled;
static _Atomic uint64_t driver_snapshot_tokens_skipped;


/* ---------------- the arrays of a cc ---------------- */

static char *driver_snapshot_array_base(const cc *c, int k, size_t *n)
{
	switch (k) {
	case DRIVER_SNAPSHOT_ARRAY_TYPES: *n = c->nty * sizeof(ctype); return (char *)c->ty;
	case DRIVER_SNAPSHOT_ARRAY_PARAMETERS: *n = c->nparams * sizeof(ast_qualified_type); return (char *)c->params;
	case DRIVER_SNAPSHOT_ARRAY_TAGS: *n = c->ntag * sizeof(tag); return (char *)c->tags;
	case DRIVER_SNAPSHOT_ARRAY_SYMBOLS: *n = c->nsym * sizeof(sym); return (char *)c->syms;
	case DRIVER_SNAPSHOT_ARRAY_NODES: *n = c->nnode * sizeof(node); return (char *)c->nodes;
	case DRIVER_SNAPSHOT_ARRAY_LISTS: *n = c->nlist * sizeof(uint32_t); return (char *)c->lists;
	case DRIVER_SNAPSHOT_ARRAY_INTEGER_POOL: *n = c->nipool * sizeof(i128); return (char *)c->ipool;
	case DRIVER_SNAPSHOT_ARRAY_FLOAT_POOL: *n = c->nfpool * sizeof(double); return (char *)c->fpool;
	case DRIVER_SNAPSHOT_ARRAY_STRINGS: *n = c->nstr * sizeof(cstr); return (char *)c->strs;
	case DRIVER_SNAPSHOT_ARRAY_LABELS: *n = c->nlabel * sizeof(label); return (char *)c->labels;
	case DRIVER_SNAPSHOT_ARRAY_ATTRIBUTES: *n = c->nattr * sizeof(attrs); return (char *)c->attr;
	case DRIVER_SNAPSHOT_ARRAY_ASM_STATEMENTS: *n = c->nasm * sizeof(ast_asm_statement); return (char *)c->asms;
	case DRIVER_SNAPSHOT_ARRAY_SCOPE_STACK: *n = c->nsstack * sizeof(uint32_t); return (char *)c->sstack;
	case DRIVER_SNAPSHOT_ARRAY_TOP_LEVEL: *n = c->ntoplevel * sizeof(uint32_t); return (char *)c->toplevel;
	case DRIVER_SNAPSHOT_ARRAY_TOP_LEVEL_ASM: *n = c->ntlasm * sizeof(uint32_t); return (char *)c->tlasm;
	case DRIVER_SNAPSHOT_ARRAY_SPANS: *n = c->nnode * sizeof(diag_span); return (char *)c->span;
	case DRIVER_SNAPSHOT_ARRAY_LAZY_TYPES: *n = c->nlzt * sizeof(cc_lazy_token); return (char *)c->lzt;
	case DRIVER_SNAPSHOT_ARRAY_LAZY_FUNCTIONS: *n = c->nlzf * sizeof(cc_lazy_function); return (char *)c->lzf;
	case DRIVER_SNAPSHOT_ARRAY_LAZY_SYMBOLS: *n = c->nlzs; return c->lzs;
	case DRIVER_SNAPSHOT_ARRAY_PARENTHESES: *n = c->nnode * sizeof(*c->paren); return (char *)c->paren;
	default: *n = (size_t)(c->hp - c->heap); return c->heap;
	}
}

/* ---------------- encoding: heap pointers and locations ---------------- */

typedef struct driver_snapshot_coder {
	const char *heap;	/* the heap the pointers point into */
	size_t heap_size;
	char *target_heap;		/* decoding: the heap they will point into */
	uint32_t location_base;		/* location base of the chain */
	bool failed;
} driver_snapshot_coder;

static void *driver_snapshot_pointer_encode(driver_snapshot_coder *k, const void *p)
{
	if (!p)
		return NULL;
	if ((const char *)p < k->heap || (const char *)p >= k->heap + k->heap_size) {
		k->failed = true;
		return NULL;
	}
	return (void *)((uintptr_t)((const char *)p - k->heap) + 1);
}

static void *driver_snapshot_pointer_decode(const driver_snapshot_coder *k, const void *p)
{
	return p ? k->target_heap + ((uintptr_t)p - 1) : NULL;
}

static inline uint32_t driver_snapshot_location_encode(driver_snapshot_coder *k, uint32_t l)
{
	if (l < k->location_base)
		return l;
	if (l - k->location_base >= DRIVER_SNAPSHOT_LOCATION_RELATIVE)
		k->failed = true;
	return DRIVER_SNAPSHOT_LOCATION_RELATIVE + (l - k->location_base);
}

static inline uint32_t driver_snapshot_location_decode(const driver_snapshot_coder *k, uint32_t l)
{
	return l >= DRIVER_SNAPSHOT_LOCATION_RELATIVE ? k->location_base + (l - DRIVER_SNAPSHOT_LOCATION_RELATIVE) : l;
}

/* the location fields of an image (arrays at img[k], heap at img[DRIVER_SNAPSHOT_ARRAY_HEAP], tags' f already coded) */
#define DRIVER_SNAPSHOT_LOCATIONS(img, c, FIX)                                                                       \
	do {                                                                                     \
		sym *sy_ = (sym *)(img)[DRIVER_SNAPSHOT_ARRAY_SYMBOLS];                                                 \
		node *nd_ = (node *)(img)[DRIVER_SNAPSHOT_ARRAY_NODES];                                              \
		tag *tg_ = (tag *)(img)[DRIVER_SNAPSHOT_ARRAY_TAGS];                                                 \
		label *lb_ = (label *)(img)[DRIVER_SNAPSHOT_ARRAY_LABELS];                                           \
		diag_span *sp_ = (diag_span *)(img)[DRIVER_SNAPSHOT_ARRAY_SPANS];                                         \
		cc_lazy_token *lt_ = (cc_lazy_token *)(img)[DRIVER_SNAPSHOT_ARRAY_LAZY_TYPES];                                              \
		cc_lazy_function *lf_ = (cc_lazy_function *)(img)[DRIVER_SNAPSHOT_ARRAY_LAZY_FUNCTIONS];                                                \
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
				field *f_ = (field *)((img)[DRIVER_SNAPSHOT_ARRAY_HEAP] + ((uintptr_t)tg_[i_].f - 1)); \
				for (uint32_t j_ = 0; j_ < tg_[i_].nfield; j_++)                 \
					f_[j_].loc = FIX(f_[j_].loc);                            \
			}                                                                        \
		}                                                                                \
	} while (0)

/* the pointer fields of the arrays (in place) */
static void driver_snapshot_pointers_translate(driver_snapshot_coder *k, char **img, const cc *c, bool enc)
{
	tag *tg = (tag *)img[DRIVER_SNAPSHOT_ARRAY_TAGS];
	cstr *st = (cstr *)img[DRIVER_SNAPSHOT_ARRAY_STRINGS];
	ast_asm_statement *as = (ast_asm_statement *)img[DRIVER_SNAPSHOT_ARRAY_ASM_STATEMENTS];

#define P(x) ((x) = enc ? driver_snapshot_pointer_encode(k, (x)) : driver_snapshot_pointer_decode(k, (x)))
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
static void driver_snapshot_state_translate(driver_snapshot_coder *k, cc *s, bool enc)
{
#define P(x) ((x) = enc ? driver_snapshot_pointer_encode(k, (x)) : driver_snapshot_pointer_decode(k, (x)))
	P(s->ptag);
	P(s->tyhash);
	P(s->fnhash);	/* (dops: the TU keeps its own, see driver_snapshot_restore) */
	for (uint32_t i = 0; i < CC_DIRECTORY_COUNT; i++) {
		P(s->odir[i]);
		P(s->tdir[i]);
		P(s->ldir[i]);
	}
#undef P
	s->switch_cases = NULL;
}

/* ---------------- snapshots ---------------- */

static bool driver_snapshot_memory_reserve(size_t n)
{
	uint64_t m = atomic_fetch_add_explicit(&driver_snapshot_memory_used, n, memory_order_relaxed);

	return m + n <= atomic_load_explicit(&driver_snapshot_memory_limit, memory_order_relaxed);
}

static void *driver_snapshot_allocate(size_t n)
{
	void *p = malloc(n);

	if (!p) {
		fputs("vcc: out of memory\n", stderr);
		abort();
	}
	return p;
}

static const driver_snapshot *driver_snapshot_ancestor_find(const driver_snapshot_node *n)
{
	for (n = n->parent; n; n = n->parent) {
		const driver_snapshot *s = atomic_load_explicit(&n->snapshot, memory_order_acquire);

		if (s)
			return s;
	}
	return NULL;
}

static _Thread_local char *driver_snapshot_image_buffer;
static _Thread_local size_t driver_snapshot_image_capacity;

static driver_snapshot *driver_snapshot_take(const cc *c, const driver_snapshot_node *n, uint32_t base)
{
	const driver_snapshot *par = driver_snapshot_ancestor_find(n);
	size_t sz[DRIVER_SNAPSHOT_ARRAY_COUNT], tot = 0, off[DRIVER_SNAPSHOT_ARRAY_COUNT];
	char *img[DRIVER_SNAPSHOT_ARRAY_COUNT];
	driver_snapshot_coder k = { c->heap, (size_t)(c->hp - c->heap), NULL, base, false };
	driver_snapshot *s;

	for (int a = 0; a < DRIVER_SNAPSHOT_ARRAY_COUNT; a++) {
		driver_snapshot_array_base(c, a, &sz[a]);
		off[a] = tot;
		tot += (sz[a] + 15) & ~(size_t)15;
	}
	if (tot > driver_snapshot_image_capacity) {
		if (driver_snapshot_image_buffer)
			munmap(driver_snapshot_image_buffer, driver_snapshot_image_capacity);
		driver_snapshot_image_capacity = (tot + ((size_t)1 << 24)) & ~(((size_t)1 << 24) - 1);
		driver_snapshot_image_buffer = mmap(NULL, driver_snapshot_image_capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (driver_snapshot_image_buffer == MAP_FAILED) {
			driver_snapshot_image_buffer = NULL, driver_snapshot_image_capacity = 0;
			return NULL;
		}
	}
	for (int a = 0; a < DRIVER_SNAPSHOT_ARRAY_COUNT; a++) {
		size_t m;

		img[a] = driver_snapshot_image_buffer + off[a];
		memcpy(img[a], driver_snapshot_array_base(c, a, &m), sz[a]);
	}
	driver_snapshot_pointers_translate(&k, img, c, true);
#define ENC(l) driver_snapshot_location_encode(&k, (l))
	DRIVER_SNAPSHOT_LOCATIONS(img, c, ENC);
#undef ENC
	if (k.failed)
		return NULL;
	s = driver_snapshot_allocate(sizeof(*s));
	s->state = *c;
	driver_snapshot_state_translate(&k, &s->state, true);
	s->state.prevloc = driver_snapshot_location_encode(&k, c->prevloc);
	if (k.failed) {
		free(s);
		return NULL;
	}
	{
		size_t own = sizeof(*s);

		for (int a = 0; a < DRIVER_SNAPSHOT_ARRAY_COUNT; a++) {
			size_t np = (sz[a] + DRIVER_SNAPSHOT_PAGE_SIZE - 1) / DRIVER_SNAPSHOT_PAGE_SIZE;

			s->arrays[a].bytes = sz[a];
			s->arrays[a].pages = driver_snapshot_allocate((np ? np : 1) * sizeof(char *));
			own += np * sizeof(char *);
			for (size_t i = 0; i < np; i++) {
				size_t len = sz[a] - i * DRIVER_SNAPSHOT_PAGE_SIZE < DRIVER_SNAPSHOT_PAGE_SIZE ? sz[a] - i * DRIVER_SNAPSHOT_PAGE_SIZE : DRIVER_SNAPSHOT_PAGE_SIZE;
				const char *src = img[a] + i * DRIVER_SNAPSHOT_PAGE_SIZE;
				char *pg;

				if (par && i * DRIVER_SNAPSHOT_PAGE_SIZE + len <= par->arrays[a].bytes && !memcmp(par->arrays[a].pages[i], src, len)) {
					s->arrays[a].pages[i] = par->arrays[a].pages[i];
					continue;
				}
				pg = driver_snapshot_allocate(DRIVER_SNAPSHOT_PAGE_SIZE);
				memcpy(pg, src, len);
				s->arrays[a].pages[i] = pg;
				own += DRIVER_SNAPSHOT_PAGE_SIZE;
			}
		}
		if (!driver_snapshot_memory_reserve(own))
			return s;	/* (published anyway: the cap stops the next ones) */
	}
	return s;
}

static void driver_snapshot_restore(cc *c, const driver_snapshot *s, uint32_t base)
{
	cc keep = *c;
	driver_snapshot_coder k = { NULL, 0, c->heap, base, false };
	char *img[DRIVER_SNAPSHOT_ARRAY_COUNT];

	for (int a = 0; a < DRIVER_SNAPSHOT_ARRAY_COUNT; a++) {
		size_t n;
		char *to = a == DRIVER_SNAPSHOT_ARRAY_HEAP ? c->heap : driver_snapshot_array_base(c, a, &n);

		for (size_t i = 0; i * DRIVER_SNAPSHOT_PAGE_SIZE < s->arrays[a].bytes; i++) {
			size_t len = s->arrays[a].bytes - i * DRIVER_SNAPSHOT_PAGE_SIZE < DRIVER_SNAPSHOT_PAGE_SIZE ? s->arrays[a].bytes - i * DRIVER_SNAPSHOT_PAGE_SIZE : DRIVER_SNAPSHOT_PAGE_SIZE;

			memcpy(to + i * DRIVER_SNAPSHOT_PAGE_SIZE, s->arrays[a].pages[i], len);
		}
		img[a] = to;
	}
	memcpy(c, &s->state, sizeof(*c));
	/* this TU's own: the token state, the arrays, options */
	c->p = keep.p;
	c->o = keep.o;
	memcpy(c->bail, keep.bail, sizeof(c->bail));
	c->rec = keep.rec;	/* (the snapshot's points into the recording TU's stack, maybe another thread's) */
	memcpy(c->tk, keep.tk, sizeof(c->tk));
	c->ntk = keep.ntk;
	c->ty = keep.ty, c->params = keep.params, c->tags = keep.tags, c->syms = keep.syms;
	c->nodes = keep.nodes, c->lists = keep.lists, c->ipool = keep.ipool, c->fpool = keep.fpool;
	c->strs = keep.strs, c->labels = keep.labels, c->attr = keep.attr, c->asms = keep.asms;
	c->sstack = keep.sstack, c->toplevel = keep.toplevel, c->tlasm = keep.tlasm;
	c->span = keep.span, c->paren = keep.paren;
	c->lzt = keep.lzt, c->lzf = keep.lzf, c->lzs = keep.lzs;
	/* malloc'd and per TU: the snapshot's would be shared (and realloc'ed) by every TU restoring it */
	c->cases = keep.cases, c->ncase = keep.ncase, c->capcase = keep.capcase, c->case_base = keep.case_base;
	c->heap = keep.heap, c->hend = keep.hend, c->ptrc = keep.ptrc;
	c->hp = c->heap + s->arrays[DRIVER_SNAPSHOT_ARRAY_HEAP].bytes;
	c->dc = keep.dc;
	c->drb = keep.drb, c->ndrb = keep.ndrb, c->cdrb = keep.cdrb;	/* (VCC_DRCOV: this TU's) */
	c->dr = keep.dr, c->dr_on = keep.dr_on;
	c->dops = keep.dops, c->ndops = keep.ndops;	/* (scratch in this TU's heap) */
	c->next_n = 0, c->ext_off = -1;	/* (ext_on's caches: the snapshot TU's options and pp) */
	driver_snapshot_state_translate(&k, c, false);
	c->prevloc = driver_snapshot_location_decode(&k, s->state.prevloc);
#define DEC(l) driver_snapshot_location_decode(&k, (l))
	DRIVER_SNAPSHOT_LOCATIONS(img, c, DEC);	/* (first: the tags' f is still coded) */
#undef DEC
	driver_snapshot_pointers_translate(&k, img, c, false);
	cc_replay_restored(c);
}

/* ---------------- the trie ---------------- */

/* every option before outfd can change what a header parses to (cc.h), so all
   of them key the trie, plus emit (code generation keeps more per decl) */
static uint64_t driver_snapshot_options_signature(const cc_options *o)
{
	const unsigned char *p = (const unsigned char *)o;
	uint64_t h = 1469598103934665603ull;

	for (size_t i = 0; i < offsetof(cc_options, outfd); i++)
		h = (h ^ p[i]) * 1099511628211ull;
	return (h ^ (uint64_t)o->emit) * 1099511628211ull;
}

static driver_snapshot_node *driver_snapshot_root_get(uint64_t sig)
{
	struct driver_snapshot_root *r, *h;

	for (;;) {
		h = atomic_load_explicit(&driver_snapshot_roots, memory_order_acquire);
		for (r = h; r; r = r->next)
			if (r->signature == sig)
				return &r->node;
		r = calloc(1, sizeof(*r));
		r->signature = sig;
		r->next = h;
		if (atomic_compare_exchange_strong_explicit(&driver_snapshot_roots, &h, r, memory_order_acq_rel, memory_order_acquire))
			return &r->node;
		free(r);
	}
}

static driver_snapshot_node *driver_snapshot_node_child(driver_snapshot_node *n, const void *e, unsigned ctr, bool make)
{
	driver_snapshot_node *h, *x;

	for (;;) {
		h = atomic_load_explicit(&n->children, memory_order_acquire);
		for (x = h; x; x = x->sibling)
			if (x->element == e && x->counter == ctr)
				return x;
		if (!make)
			return NULL;
		x = calloc(1, sizeof(*x));
		x->element = e;
		x->counter = ctr;
		x->parent = n;
		x->sibling = h;
		if (atomic_compare_exchange_strong_explicit(&n->children, &h, x, memory_order_acq_rel, memory_order_acquire))
			return x;
		free(x);
	}
}

/* ---------------- per TU ---------------- */

static void driver_snapshot_environment_read(void)
{
	const char *m = getenv("VCC_DC_MAX_MB");

	atomic_store(&driver_snapshot_memory_limit, (uint64_t)(m ? atoll(m) : 256) << 20);
	driver_snapshot_disabled = getenv("VCC_NODC") != NULL;
}

void driver_declaration_cache_begin(cc *c)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	pthread_once(&once, driver_snapshot_environment_read);	/* worker threads race to the first TU */
	c->dc = NULL;
	if (driver_snapshot_disabled)
		return;
	c->dc = calloc(1, sizeof(struct driver_declaration_cache_state));
	c->dc->node = driver_snapshot_root_get(driver_snapshot_options_signature(c->o));
}

void driver_declaration_cache_end(cc *c)
{
	if (c->dc)
		atomic_fetch_add_explicit(&driver_snapshot_tokens_skipped, pp_skipped_token_count(c->p), memory_order_relaxed);
	free(c->dc);
	c->dc = NULL;
}

void driver_declaration_cache_materialize(cc *c)
{
	struct driver_declaration_cache_state *d = c->dc;
	const driver_snapshot *s = atomic_load_explicit(&d->target->snapshot, memory_order_acquire);

	d->stale = false;
	driver_snapshot_restore(c, s, d->location_base);
	atomic_fetch_add_explicit(&driver_snapshot_restore_count, 1, memory_order_relaxed);
}

static void driver_snapshot_mark_dead(cc *c)
{
	if (c->dc->stale)
		driver_declaration_cache_materialize(c);
	c->dc->dead = true;
}

/* no snapshot from here on: state the snapshots do not hold (#pragma weak) */
void driver_declaration_cache_kill(cc *c)
{
	if (c->dc && !c->dc->dead)
		driver_snapshot_mark_dead(c);
}

void driver_declaration_cache_boundary_slow(cc *c)
{
	struct driver_declaration_cache_state *d = c->dc;

	for (;;) {
		uint32_t n;
		const pp_topev *ev = pp_top_events(c->p, &n);
		uint64_t N = pp_token_count(c->p);

		if (!ev || c->ntk != 1) {
			driver_snapshot_mark_dead(c);
			return;
		}
		for (; d->events < n; d->events++) {
			const pp_topev *E = &ev[d->events];

			if (!E->end) {
				if (d->is_open || E->pos != d->position) {
					driver_snapshot_mark_dead(c);
					return;
				}
				if (!d->node->parent)
					d->location_base = E->base;
				d->open_event = *E;
				d->is_open = true;
				continue;
			}
			/* a region ended: right before tk[0], or the chain is over */
			if (!d->is_open || !E->e || E->pos + 1 != N) {
				driver_snapshot_mark_dead(c);
				return;
			}
			d->is_open = false;
			d->position = E->pos;
			d->node = driver_snapshot_node_child(d->node, E->e, d->open_event.ctr, true);
			atomic_fetch_add_explicit(&d->node->visits, 1, memory_order_relaxed);
			if (!d->stale && !c->dr_on && atomic_load_explicit(&d->node->visits, memory_order_relaxed) >= 2 &&
			    !atomic_load_explicit(&d->node->snapshot, memory_order_acquire) && !c->errors && !c->ndiag &&
			    !pp_error_count(c->p) && atomic_load_explicit(&driver_snapshot_memory_used, memory_order_relaxed) <
			    atomic_load_explicit(&driver_snapshot_memory_limit, memory_order_relaxed)) {
				driver_snapshot *s = driver_snapshot_take(c, d->node, d->location_base), *z = NULL;

				if (s && atomic_compare_exchange_strong_explicit(&d->node->snapshot, &z, s,
										 memory_order_release, memory_order_relaxed))
					atomic_fetch_add_explicit(&driver_snapshot_take_count, 1, memory_order_relaxed);
			}
		}
		/* tk[0] is the first token of a replayed region whose state is known: skip it */
		if (d->is_open && d->open_event.hit && d->open_event.pos + 1 == N) {
			driver_snapshot_node *x = driver_snapshot_node_child(d->node, d->open_event.e, d->open_event.ctr, false);

			if (x && atomic_load_explicit(&x->snapshot, memory_order_acquire) && pp_top_region_skip(c->p)) {
				d->stale = true;
				d->target = x;
				atomic_fetch_add_explicit(&driver_snapshot_region_count, 1, memory_order_relaxed);
				cc_token_fetch_out_of_line(c, &c->tk[0]);	/* (a pragma restores first) */
				continue;
			}
		}
		if (d->stale)
			driver_declaration_cache_materialize(c);
		return;
	}
}

void driver_declaration_cache_statistics_format(char *buf, size_t n)
{
	snprintf(buf, n, "decl cache: %lu restores, %lu regions (%.1f M tokens) skipped, %lu snapshots, %.1f MB",
		 atomic_load(&driver_snapshot_restore_count), atomic_load(&driver_snapshot_region_count), (double)atomic_load(&driver_snapshot_tokens_skipped) / 1e6,
		 atomic_load(&driver_snapshot_take_count), (double)atomic_load(&driver_snapshot_memory_used) / (1 << 20));
}
