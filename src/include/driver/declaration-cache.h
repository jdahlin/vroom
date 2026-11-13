#ifndef VCC_CCSNAP_H
#define VCC_CCSNAP_H
/*
 * declaration-cache.h: the declaration cache's interface: the parser's state after a TU's leading headers,
 * kept and restored (driver/declaration-cache.c has the model).
 *
 * The front end calls driver_declaration_cache_begin and driver_declaration_cache_end once per TU (frontend/c/translation-unit.c) and
 * driver_declaration_cache_boundary at each file-scope declaration boundary; a boundary where the preprocessor has
 * reported a new top-level region is the slow path (driver_declaration_cache_boundary_slow), which may restore a
 * snapshot instead of parsing the region again.  The state is "stale" while a restore is pending,
 * and anything that needs the real state first calls driver_declaration_cache_materialize (token.c, before a pragma
 * acts); driver_declaration_cache_kill turns the cache off for the rest of the TU (after an error, or a pending weak
 * pragma), since later snapshots would be wrong.  Per-TU state is struct driver_declaration_cache_state (cc->dc); the
 * snapshots are shared by the TUs of the process.  driver_declaration_cache_statistics_format prints what vccd's counters show.
 */
#include "frontend/c/ccint.h"

struct driver_snapshot_node;

struct driver_declaration_cache_state {
	struct driver_snapshot_node *node;	/* the chain so far */
	struct driver_snapshot_node *target;	/* stale: the node whose snapshot is the real state */
	uint64_t position;		/* where the next region must start */
	uint32_t events;		/* events consumed */
	uint32_t location_base;		/* location base of the chain's first region */
	pp_topev open_event;		/* a region started, not ended */
	bool is_open, dead, stale;	/* stale: a restore is pending */
};

void driver_declaration_cache_begin(cc *c);
void driver_declaration_cache_end(cc *c);
void driver_declaration_cache_boundary_slow(cc *c);
void driver_declaration_cache_materialize(cc *c);
void driver_declaration_cache_kill(cc *c);
void driver_declaration_cache_statistics_format(char *buf, size_t n);
/* frontend/c/token.c: the next token, pragmas applied */
void cc_token_fetch_out_of_line(cc *c, ptok *t);

/* at a file-scope declaration boundary (tk[0] fetched, nothing parsed of it) */
static inline void driver_declaration_cache_boundary(cc *c)
{
	struct driver_declaration_cache_state *d = c->dc;
	uint32_t n;

	if (likely(!d || d->dead))
		return;
	if (pp_top_events(c->p, &n) && n == d->events && !d->stale)
		return;
	driver_declaration_cache_boundary_slow(c);
}

static inline bool driver_declaration_cache_is_stale(const cc *c)
{
	return c->dc && c->dc->stale;
}
#endif
