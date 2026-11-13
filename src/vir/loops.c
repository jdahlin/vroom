/*
 * loops.c: the loop forest as ranges of the block array (SPEC-vir.md 15.5), built by
 * vir_loops_build for the register allocator and the lowerings.
 *
 * The spec gives the forest to vir_canon (22.5), which does not build it yet; until it does, callers
 * (the lowerings' function.c in arch/a64/vir and arch/x64/vir, viralloc) call vir_loops_build themselves,
 * after vir_canon, and this file is the stand-in with the interface the forest will keep (vir_loops).
 *
 * A loop is a range [head, end] of blocks: for every edge s -> t with t <= s the range t..s.  Ranges
 * with the same head are one loop (the largest end).  Ranges that overlap partially are merged into
 * one, so the result is a properly nested forest; on irreducible control flow a merged range has
 * several entries and its head is only its first block.  Cost: one pass over the predecessor lists, a
 * sort of the loops, one pass over the blocks.  The scratch is per thread; the result is valid
 * until the next call on that thread.
 */
#include "virimpl.h"
#include "vir/alloc.h"

static _Thread_local struct lctx {
	vir_loop *l;
	uint32_t *inner, *stk;
	uint32_t (*rng)[2];
} lx;

static void lx_init(void)
{
	if (lx.l)
		return;
	lx.l = vreserve((size_t)VIR_MAX_BLK * sizeof(vir_loop), MAP_NORESERVE, "vir: loops");
	lx.inner = vreserve((size_t)VIR_MAX_BLK * 4, MAP_NORESERVE, "vir: loops");
	lx.stk = vreserve((size_t)VIR_MAX_BLK * 4, MAP_NORESERVE, "vir: loops");
	lx.rng = vreserve((size_t)VIR_MAX_BLK * 8, MAP_NORESERVE, "vir: loops");
}

static int by_head(const void *a, const void *b)
{
	const vir_loop *x = a, *y = b;

	return x->head < y->head ? -1 : x->head > y->head;
}

bool vir_loops_build(vir_fn *f, vir_loops *lp)
{
	uint32_t n = f->nblk, sp = 0, nl = 0;
	uint32_t (*st)[2] = lx.rng;

	lx_init();
	st = lx.rng;
	lp->l = lx.l;
	lp->inner = lx.inner;
	lp->n = 0;
	if (!vir_cfg(f))
		return false;
	/* phase 1: per head (in block order, so ranges arrive sorted by start) the largest
	 * back-edge source; a range that ends inside the open one on top and continues past
	 * it overlaps partially and is merged, which can make the top overlap its parent */
	for (uint32_t b = 0; b < n; b++) {
		const vir_blk *k = &f->blk[b];
		uint32_t e = VIR_NOBLK;

		for (uint32_t j = 0; j < k->npred; j++) {
			uint32_t p = f->extra[k->pred + j];

			if (p >= b && (e == VIR_NOBLK || p > e))
				e = p;
		}
		if (e == VIR_NOBLK)
			continue;
		while (sp && st[sp - 1][1] < b) {
			sp--;
			lx.l[nl++] = (vir_loop){ st[sp][0], st[sp][1], VIR_NOBLK, 0 };
		}
		if (sp && st[sp - 1][1] < e) {
			st[sp - 1][1] = e;
			while (sp >= 2 && st[sp - 2][1] < st[sp - 1][1]) {
				st[sp - 2][1] = st[sp - 1][1];
				sp--;
			}
			continue;
		}
		st[sp][0] = b;
		st[sp][1] = e;
		sp++;
	}
	while (sp) {
		sp--;
		lx.l[nl++] = (vir_loop){ st[sp][0], st[sp][1], VIR_NOBLK, 0 };
	}
	qsort(lx.l, nl, sizeof(vir_loop), by_head);
	/* phase 2: parents, depths and each block's innermost loop */
	sp = 0;
	for (uint32_t b = 0, i = 0; b < n; b++) {
		while (sp && lx.l[lx.stk[sp - 1]].end < b)
			sp--;
		while (i < nl && lx.l[i].head == b) {
			lx.l[i].parent = sp ? lx.stk[sp - 1] : VIR_NOBLK;
			lx.l[i].depth = sp + 1;
			lx.stk[sp++] = i++;
		}
		lx.inner[b] = sp ? lx.stk[sp - 1] : VIR_NOBLK;
	}
	lp->n = nl;
	return true;
}
