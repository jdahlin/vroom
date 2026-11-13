/* vcc preprocessor: what the C front end asks of header-cache hits: top-level events, hit regions, skipping a hit */
#include "pp-internal.h"

const pp_topev *pp_top_events(const pp *p, uint32_t *n)
{
	*n = p->top_event_count;
	return p->tev_full ? NULL : p->top_events ? p->top_events : (const pp_topev *)"";
}

uint64_t pp_token_count(const pp *p)
{
	return p->tokens_delivered;
}

uint64_t pp_skipped_token_count(const pp *p)
{
	return p->tokens_skipped;
}

bool pp_hit_open_get(const pp *p, uint64_t *b, const void **he, uint32_t *base, unsigned *ctr, uint32_t *span)
{
	if (!p->open_hit_count)
		return false;
	*b = p->open_hits[p->open_hit_count - 1].pos;
	*he = p->open_hits[p->open_hit_count - 1].he;
	if (base)
		*base = p->open_hits[p->open_hit_count - 1].base, *ctr = p->open_hits[p->open_hit_count - 1].ctr,
		*span = ((const pp_header_cache_entry *)*he)->span;
	return true;
}

uint64_t pp_hit_token_count(const void *he)
{
	return ((const pp_header_cache_entry *)he)->total;
}

const pp_hitreg *pp_hit_regions(const pp *p, uint32_t *n)
{
	*n = p->ndrh;
	return p->hit_regions;
}

/* VCC_DRCOV: a header-cache hit just applied; its replay (if any) is the context at depth d */
VPRIV void pp_coverage_hit_begin(pp *p, int d0)
{
	if (!p->hit_regions)
		p->hit_regions = varena_alloc(&p->perm, PP_COVERAGE_HIT_MAX * sizeof(pp_hitreg));
	if (p->depth > d0 && p->cx[d0].type == PP_CONTEXT_SERVED && p->cx[d0].he == p->header_cache_last_entry) {
		if (p->open_hit_count < 64) {
			p->open_hits[p->open_hit_count].pos = p->tokens_delivered, p->open_hits[p->open_hit_count].depth = p->depth;
			p->open_hits[p->open_hit_count].base = p->cx[d0].loc, p->open_hits[p->open_hit_count].ctr = p->cx[d0].cbase;
			p->open_hits[p->open_hit_count++].he = p->header_cache_last_entry;
		}
	}
}

/* VCC_DRCOV: the replay context at the top of the stack is done */
VPRIV void pp_coverage_hit_end(pp *p)
{
	if (p->open_hit_count && p->open_hits[p->open_hit_count - 1].depth == p->depth) {
		p->open_hit_count--;
		if (p->ndrh < PP_COVERAGE_HIT_MAX && p->tokens_delivered > p->open_hits[p->open_hit_count].pos)
			p->hit_regions[p->ndrh++] = (pp_hitreg){ p->open_hits[p->open_hit_count].pos, p->tokens_delivered, (uint32_t)p->nfile,
						    p->open_hits[p->open_hit_count].he };
	}
}

int pp_top_region_skip(pp *p)
{
	const pp_topev *s = p->top_event_count && !p->tev_full ? &p->top_events[p->top_event_count - 1] : NULL;
	int d0 = -1;

	if (!s || s->end || !s->hit || s->pos + 1 != p->tokens_delivered)
		return 0;
	for (int d = p->depth - 1; d >= 0; d--)
		if (p->cx[d].type == PP_CONTEXT_SERVED && p->cx[d].top == 2) {
			d0 = d;
			break;
		}
	if (d0 < 0 || p->cx[d0].he != s->e)
		return 0;
	for (int d = d0 + 1; d < p->depth; d++)
		if (p->cx[d].type != PP_CONTEXT_SERVED)
			return 0;
	p->tokens_skipped += p->cx[d0].he->total;
	while (p->open_hit_count && p->open_hits[p->open_hit_count - 1].depth > d0)
		p->open_hit_count--;	/* (VCC_DRCOV: dropped, not counted as a hit) */
	p->pend |= p->cx[d0].he->pend_out;
	p->depth = d0;
	p->tokens_delivered = s->pos;
	pp_top_event_record(p, s->e, 0, 0, 1, 1);
	return 1;
}
/*
 * Per-region declaration replay (frontend/c/replay.c): drop the rest of the
 * innermost open hit, whose first token was the last one delivered, as
 * pp_top_region_skip does for a top-level region.  0 if that is not the case.
 */
int pp_hit_skip(pp *p, int dry)
{
	int d0, top;

	if (!p->open_hit_count || p->open_hits[p->open_hit_count - 1].pos + 1 != p->tokens_delivered)
		return 0;
	d0 = p->open_hits[p->open_hit_count - 1].depth - 1;
	if (d0 < 0 || p->cx[d0].type != PP_CONTEXT_SERVED || p->cx[d0].he != p->open_hits[p->open_hit_count - 1].he)
		return 0;
	for (int d = d0 + 1; d < p->depth; d++)
		if (p->cx[d].type != PP_CONTEXT_SERVED)
			return 0;
	if (p->rec.on && !p->cx[d0].norec)
		return 0;	/* an enclosing header is being recorded: it needs the tokens */
	top = p->cx[d0].top;
	if (top == 2) {
		const pp_topev *t = p->top_event_count && !p->tev_full ? &p->top_events[p->top_event_count - 1] : NULL;

		if (!t || t->end || !t->hit || t->pos + 1 != p->tokens_delivered || t->e != p->cx[d0].he)
			return 0;
		return dry ? 1 : pp_top_region_skip(p);
	}
	if (dry)
		return 1;
	p->tokens_skipped += p->cx[d0].he->total;
	while (p->open_hit_count && p->open_hits[p->open_hit_count - 1].depth > d0)
		p->open_hit_count--;
	if (top)
		p->pend |= p->cx[d0].he->pend_out;
	p->depth = d0;
	p->tokens_delivered--;
	return 1;
}
