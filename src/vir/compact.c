/*
 * vir_function_compact: what a pass that only rewrites operands and turns
 * instructions into nops calls before it returns (DESIGN-vir-optimizer.md 3).
 * Two sweeps in place instead of a second vir_canon (which costs a dominator
 * tree and a copy): mark the constants still used, then shift the block
 * region over the nops, renumbering operands through f->tmp.  The order of
 * constants (by first use) and of instructions stays, so the result is
 * canonical again, and no block, edge or terminator moves: f->valid stays
 * (predecessor lists hold block ids; idom, pre and post do not change).
 *
 * The input is canonical except that instructions in the block region may be
 * nops (block parameters are never nops).  Block ranges are contiguous and in
 * block order, as vir_canon makes them.
 */
#include "virimpl.h"
#include "vir/pass.h"

#define VIR_COMPACT_NONE 0xffffffffu

uint32_t vir_function_compact(vir_fn *f)
{
	uint32_t *tmp = f->tmp, nins = f->nins, nconst = f->nconst, nc = 1, k;
	uint32_t q;

	if (!nconst || nconst > nins || !f->nblk)
		return 0;
	for (uint32_t i = 0; i < nins; i++)
		tmp[i] = VIR_COMPACT_NONE;
	/* 1. the constants an instruction still uses (tmp = 0) */
	for (uint32_t b = 0; b < f->nblk; b++) {
		const vir_blk *kb = &f->blk[b];

		for (uint32_t i = kb->first; i < kb->first + kb->n && i < nins; i++) {
			vir_ins y = f->ins[i];
			vir_oit it;
			uint32_t *p;

			if (y.op == VO_NOP)
				continue;
			vir_oit_init(&it, f, &y, false);
			while ((p = vir_oit_next(&it)))
				if (*p >= 1 && *p < nconst)
					tmp[*p] = 0;
		}
	}
	/* 2. the new index of every kept value */
	tmp[0] = 0;
	for (uint32_t i = 1; i < nconst; i++)
		if (tmp[i] == 0)
			tmp[i] = nc++;
	k = nc;
	for (uint32_t b = 0; b < f->nblk; b++) {
		const vir_blk *kb = &f->blk[b];

		for (uint32_t i = kb->first; i < kb->first + kb->n && i < nins; i++)
			if (f->ins[i].op != VO_NOP)
				tmp[i] = k++;
	}
	if (k == nins)	/* nothing to drop */
		return 0;
	/* 3. the source locations: a run starts at its first instruction that is kept */
	if (f->nloc) {
		uint32_t nl = 0;

		q = nconst;
		for (uint32_t j = 0; j < f->nloc; j++) {
			vir_loc r = f->loc[j];

			if (q < r.first)
				q = r.first;
			while (q < nins && tmp[q] == VIR_COMPACT_NONE)
				q++;
			if (q >= nins)
				break;
			if (nl && f->loc[nl - 1].first == tmp[q])
				nl--;	/* the earlier run lost all its instructions */
			if (nl && f->loc[nl - 1].loc == r.loc)
				continue;
			f->loc[nl].first = tmp[q];
			f->loc[nl].loc = r.loc;
			nl++;
		}
		f->nloc = nl;
	}
	/* 4. move the constants, then the block instructions, renumbering operands (never reading ins[] of a
	      value, so that overwriting an old slot is safe: kept values only move down) */
	for (uint32_t i = 1; i < nconst; i++)
		if (tmp[i] != VIR_COMPACT_NONE && tmp[i] != i)
			f->ins[tmp[i]] = f->ins[i];
	for (uint32_t b = 0; b < f->nblk; b++) {
		vir_blk *kb = &f->blk[b];
		uint32_t o = 0, end = kb->first + kb->n;

		for (uint32_t i = kb->first; i < end && i < nins; i++) {
			vir_ins y = f->ins[i];
			vir_oit it;
			uint32_t *p;

			if (y.op == VO_NOP)
				continue;
			vir_oit_init(&it, f, &y, false);
			while ((p = vir_oit_next(&it)))
				*p = *p < nins && tmp[*p] != VIR_COMPACT_NONE ? tmp[*p] : 0;
			f->ins[tmp[i]] = y;
			o++;
		}
		kb->n = o;
	}
	/* block starts: the first kept instruction's new index (an empty range cannot occur: a block ends in a terminator) */
	{
		uint32_t at = nc;

		for (uint32_t b = 0; b < f->nblk; b++) {
			f->blk[b].first = at;
			at += f->blk[b].n;
		}
	}
	f->nins = k;
	f->nconst = nc;
	vir_ctab_rebuild(f);
	return nins - k;
}
