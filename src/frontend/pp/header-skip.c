/* vcc preprocessor: what the C front end asks of header-cache hits: top-level events, hit regions, skipping a hit */
#include "ppimpl.h"

const pp_topev *pp_top_events(const pp *p, uint32_t *n)
{
	*n = p->ntev;
	return p->tev_full ? NULL : p->tev ? p->tev : (const pp_topev *)"";
}

uint64_t pp_ntok(const pp *p)
{
	return p->ntok_out;
}

uint64_t pp_skipped(const pp *p)
{
	return p->skipped_tok;
}

bool pp_open_hit(const pp *p, uint64_t *b, const void **he, uint32_t *base, unsigned *ctr, uint32_t *span)
{
	if (!p->ndrs)
		return false;
	*b = p->drs[p->ndrs - 1].pos;
	*he = p->drs[p->ndrs - 1].he;
	if (base)
		*base = p->drs[p->ndrs - 1].base, *ctr = p->drs[p->ndrs - 1].ctr,
		*span = ((const hent *)*he)->span;
	return true;
}

uint64_t pp_hit_ntok(const void *he)
{
	return ((const hent *)he)->total;
}

const pp_hitreg *pp_hit_regions(const pp *p, uint32_t *n)
{
	*n = p->ndrh;
	return p->drh;
}

/* VCC_DRCOV: a header-cache hit just applied; its replay (if any) is the context at depth d */
VPRIV void drcov_hit(pp *p, int d0)
{
	if (!p->drh)
		p->drh = varena_alloc(&p->perm, DRCOV_MAX * sizeof(pp_hitreg));
	if (p->depth > d0 && p->cx[d0].type == C_REPLAY && p->cx[d0].he == p->hc_last) {
		if (p->ndrs < 64) {
			p->drs[p->ndrs].pos = p->ntok_out, p->drs[p->ndrs].depth = p->depth;
			p->drs[p->ndrs].base = p->cx[d0].loc, p->drs[p->ndrs].ctr = p->cx[d0].cbase;
			p->drs[p->ndrs++].he = p->hc_last;
		}
	}
}

/* VCC_DRCOV: the replay context at the top of the stack is done */
VPRIV void drcov_end(pp *p)
{
	if (p->ndrs && p->drs[p->ndrs - 1].depth == p->depth) {
		p->ndrs--;
		if (p->ndrh < DRCOV_MAX && p->ntok_out > p->drs[p->ndrs].pos)
			p->drh[p->ndrh++] = (pp_hitreg){ p->drs[p->ndrs].pos, p->ntok_out, (uint32_t)p->nfile,
						    p->drs[p->ndrs].he };
	}
}

int pp_skip_top(pp *p)
{
	const pp_topev *s = p->ntev && !p->tev_full ? &p->tev[p->ntev - 1] : NULL;
	int d0 = -1;

	if (!s || s->end || !s->hit || s->pos + 1 != p->ntok_out)
		return 0;
	for (int d = p->depth - 1; d >= 0; d--)
		if (p->cx[d].type == C_REPLAY && p->cx[d].top == 2) {
			d0 = d;
			break;
		}
	if (d0 < 0 || p->cx[d0].he != s->e)
		return 0;
	for (int d = d0 + 1; d < p->depth; d++)
		if (p->cx[d].type != C_REPLAY)
			return 0;
	p->skipped_tok += p->cx[d0].he->total;
	while (p->ndrs && p->drs[p->ndrs - 1].depth > d0)
		p->ndrs--;	/* (VCC_DRCOV: dropped, not counted as a hit) */
	p->pend |= p->cx[d0].he->pend_out;
	p->depth = d0;
	p->ntok_out = s->pos;
	top_event(p, s->e, 0, 0, 1, 1);
	return 1;
}
/*
 * Per-region declaration replay (frontend/c/replay.c): drop the rest of the
 * innermost open hit, whose first token was the last one delivered, as
 * pp_skip_top does for a top-level region.  0 if that is not the case.
 */
int pp_skip_hit(pp *p, int dry)
{
	int d0, top;

	if (!p->ndrs || p->drs[p->ndrs - 1].pos + 1 != p->ntok_out)
		return 0;
	d0 = p->drs[p->ndrs - 1].depth - 1;
	if (d0 < 0 || p->cx[d0].type != C_REPLAY || p->cx[d0].he != p->drs[p->ndrs - 1].he)
		return 0;
	for (int d = d0 + 1; d < p->depth; d++)
		if (p->cx[d].type != C_REPLAY)
			return 0;
	if (p->rec.on && !p->cx[d0].norec)
		return 0;	/* an enclosing header is being recorded: it needs the tokens */
	top = p->cx[d0].top;
	if (top == 2) {
		const pp_topev *t = p->ntev && !p->tev_full ? &p->tev[p->ntev - 1] : NULL;

		if (!t || t->end || !t->hit || t->pos + 1 != p->ntok_out || t->e != p->cx[d0].he)
			return 0;
		return dry ? 1 : pp_skip_top(p);
	}
	if (dry)
		return 1;
	p->skipped_tok += p->cx[d0].he->total;
	while (p->ndrs && p->drs[p->ndrs - 1].depth > d0)
		p->ndrs--;
	if (top)
		p->pend |= p->cx[d0].he->pend_out;
	p->depth = d0;
	p->ntok_out--;
	return 1;
}
