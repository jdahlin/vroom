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
#include "vir-internal.h"
#include "vir/register-allocation.h"

static _Thread_local struct lctx {
	vir_loop *l;
	uint32_t *inner, *stk;
	uint32_t (*rng)[2];
} vir_loops_thread_arrays;

static void vir_loops_thread_init(void)
{
	if (vir_loops_thread_arrays.l)
		return;
	vir_loops_thread_arrays.l = vreserve((size_t)VIR_MAX_BLK * sizeof(vir_loop), MAP_NORESERVE, "vir: loops");
	vir_loops_thread_arrays.inner = vreserve((size_t)VIR_MAX_BLK * 4, MAP_NORESERVE, "vir: loops");
	vir_loops_thread_arrays.stk = vreserve((size_t)VIR_MAX_BLK * 4, MAP_NORESERVE, "vir: loops");
	vir_loops_thread_arrays.rng = vreserve((size_t)VIR_MAX_BLK * 8, MAP_NORESERVE, "vir: loops");
}

static int by_head(const void *a, const void *b)
{
	const vir_loop *x = a, *y = b;

	return x->head < y->head ? -1 : x->head > y->head;
}

bool vir_loops_build(vir_function *f, vir_loops *lp)
{
	uint32_t n = f->block_count, sp = 0, nl = 0;
	uint32_t (*st)[2] = vir_loops_thread_arrays.rng;

	vir_loops_thread_init();
	st = vir_loops_thread_arrays.rng;
	lp->loops = vir_loops_thread_arrays.l;
	lp->inner = vir_loops_thread_arrays.inner;
	lp->count = 0;
	if (!vir_cfg_build(f))
		return false;
	/* phase 1: per head (in block order, so ranges arrive sorted by start) the largest
	 * back-edge source; a range that ends inside the open one on top and continues past
	 * it overlaps partially and is merged, which can make the top overlap its parent */
	for (uint32_t b = 0; b < n; b++) {
		const vir_block *k = &f->blocks[b];
		uint32_t e = VIR_BLOCK_NONE;

		for (uint32_t j = 0; j < k->predecessor_count; j++) {
			uint32_t p = f->extra[k->predecessor_first + j];

			if (p >= b && (e == VIR_BLOCK_NONE || p > e))
				e = p;
		}
		if (e == VIR_BLOCK_NONE)
			continue;
		while (sp && st[sp - 1][1] < b) {
			sp--;
			vir_loops_thread_arrays.l[nl++] = (vir_loop){ st[sp][0], st[sp][1], VIR_BLOCK_NONE, 0 };
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
		vir_loops_thread_arrays.l[nl++] = (vir_loop){ st[sp][0], st[sp][1], VIR_BLOCK_NONE, 0 };
	}
	qsort(vir_loops_thread_arrays.l, nl, sizeof(vir_loop), by_head);
	/* phase 2: parents, depths and each block's innermost loop */
	sp = 0;
	for (uint32_t b = 0, i = 0; b < n; b++) {
		while (sp && vir_loops_thread_arrays.l[vir_loops_thread_arrays.stk[sp - 1]].end < b)
			sp--;
		while (i < nl && vir_loops_thread_arrays.l[i].head == b) {
			vir_loops_thread_arrays.l[i].parent = sp ? vir_loops_thread_arrays.stk[sp - 1] : VIR_BLOCK_NONE;
			vir_loops_thread_arrays.l[i].depth = sp + 1;
			vir_loops_thread_arrays.stk[sp++] = i++;
		}
		vir_loops_thread_arrays.inner[b] = sp ? vir_loops_thread_arrays.stk[sp - 1] : VIR_BLOCK_NONE;
	}
	lp->count = nl;
	return true;
}
