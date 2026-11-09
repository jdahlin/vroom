#ifndef VCC_CCSNAP_H
#define VCC_CCSNAP_H
/* the declaration cache (ccsnap.c): parser snapshots after leading top-level headers */
#include "ccint.h"

struct dcnode;

struct dcst {
	struct dcnode *node;	/* the chain so far */
	struct dcnode *target;	/* stale: the node whose snapshot is the real state */
	uint64_t pos;		/* where the next region must start */
	uint32_t ev;		/* events consumed */
	uint32_t base;		/* location base of the chain's first region */
	pp_topev open;		/* a region started, not ended */
	bool is_open, dead, stale;	/* stale: a restore is pending */
};

void dc_begin(cc *c);
void dc_end(cc *c);
void dc_boundary_slow(cc *c);
void dc_materialize(cc *c);
void dc_kill(cc *c);
void dc_stats(char *buf, size_t n);
/* cc/token.c: the next token, pragmas applied */
void cc_fetch(cc *c, ptok *t);

/* at a file-scope declaration boundary (tk[0] fetched, nothing parsed of it) */
static inline void dc_boundary(cc *c)
{
	struct dcst *d = c->dc;
	uint32_t n;

	if (likely(!d || d->dead))
		return;
	if (pp_top_events(c->p, &n) && n == d->ev && !d->stale)
		return;
	dc_boundary_slow(c);
}

static inline bool dc_stale(const cc *c)
{
	return c->dc && c->dc->stale;
}
#endif
