/*
 * vir_function_compact: what a pass that only rewrites operands and turns
 * instructions into nops calls before it returns (DESIGN-vir-optimizer.md 3).
 * Two sweeps in place instead of a second vir_canon (which costs a dominator
 * tree and a copy): mark the constants still used, then shift the block
 * region over the nops, renumbering operands through f->scratch.  The order of
 * constants (by first use) and of instructions stays, so the result is
 * canonical again, and no block, edge or terminator moves: f->valid stays
 * (predecessor lists hold block ids; idom, pre and post do not change).
 *
 * The input is canonical except that instructions in the block region may be
 * nops (block parameters are never nops).  Block ranges are contiguous and in
 * block order, as vir_canon makes them.
 */
#include "vir-internal.h"
#include "vir/pass.h"

#define VIR_COMPACT_NONE 0xffffffffu

uint32_t vir_function_compact(vir_function *f)
{
	uint32_t *tmp = f->scratch, nins = f->insn_count, nconst = f->constant_count, nc = 1, k;
	uint32_t q;

	if (!nconst || nconst > nins || !f->block_count)
		return 0;
	for (uint32_t i = 0; i < nins; i++)
		tmp[i] = VIR_COMPACT_NONE;
	/* 1. the constants an instruction still uses (tmp = 0) */
	for (uint32_t b = 0; b < f->block_count; b++) {
		const vir_block *kb = &f->blocks[b];

		for (uint32_t i = kb->first; i < kb->first + kb->insn_count && i < nins; i++) {
			vir_insn y = f->insns[i];
			vir_operand_iterator it;
			uint32_t *p;

			if (y.op == VIR_OP_NOP)
				continue;
			vir_operand_iterator_init(&it, f, &y, false);
			while ((p = vir_operand_iterator_next(&it)))
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
	for (uint32_t b = 0; b < f->block_count; b++) {
		const vir_block *kb = &f->blocks[b];

		for (uint32_t i = kb->first; i < kb->first + kb->insn_count && i < nins; i++)
			if (f->insns[i].op != VIR_OP_NOP)
				tmp[i] = k++;
	}
	if (k == nins)	/* nothing to drop */
		return 0;
	/* 3. the source locations: a run starts at its first instruction that is kept */
	if (f->location_count) {
		uint32_t nl = 0;

		q = nconst;
		for (uint32_t j = 0; j < f->location_count; j++) {
			vir_location r = f->locations[j];

			if (q < r.first)
				q = r.first;
			while (q < nins && tmp[q] == VIR_COMPACT_NONE)
				q++;
			if (q >= nins)
				break;
			if (nl && f->locations[nl - 1].first == tmp[q])
				nl--;	/* the earlier run lost all its instructions */
			if (nl && f->locations[nl - 1].source_location == r.source_location)
				continue;
			f->locations[nl].first = tmp[q];
			f->locations[nl].source_location = r.source_location;
			nl++;
		}
		f->location_count = nl;
	}
	/* 4. move the constants, then the block instructions, renumbering operands (never reading ins[] of a
	      value, so that overwriting an old slot is safe: kept values only move down) */
	for (uint32_t i = 1; i < nconst; i++)
		if (tmp[i] != VIR_COMPACT_NONE && tmp[i] != i)
			f->insns[tmp[i]] = f->insns[i];
	for (uint32_t b = 0; b < f->block_count; b++) {
		vir_block *kb = &f->blocks[b];
		uint32_t o = 0, end = kb->first + kb->insn_count;

		for (uint32_t i = kb->first; i < end && i < nins; i++) {
			vir_insn y = f->insns[i];
			vir_operand_iterator it;
			uint32_t *p;

			if (y.op == VIR_OP_NOP)
				continue;
			vir_operand_iterator_init(&it, f, &y, false);
			while ((p = vir_operand_iterator_next(&it)))
				*p = *p < nins && tmp[*p] != VIR_COMPACT_NONE ? tmp[*p] : 0;
			f->insns[tmp[i]] = y;
			o++;
		}
		kb->insn_count = o;
	}
	/* block starts: the first kept instruction's new index (an empty range cannot occur: a block ends in a terminator) */
	{
		uint32_t at = nc;

		for (uint32_t b = 0; b < f->block_count; b++) {
			f->blocks[b].first = at;
			at += f->blocks[b].insn_count;
		}
	}
	f->insn_count = k;
	f->constant_count = nc;
	vir_constant_table_rebuild(f);
	return nins - k;
}
