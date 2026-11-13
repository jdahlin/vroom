/* The simplifier's loads across blocks. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

/*
 * A load across blocks.  The memory state above ends at every join and at every store.  A
 * second table keeps each load by base and constant offset for the blocks it dominates;
 * a later load of the same bytes is that value when nothing that may write them lies
 * between the two.  "Between" is by position in reverse postorder: a path from the first
 * load L1 (after its last execution) to the second, L2, never visits a block before L1's
 * (one that L1's block does not dominate leads back through L1), and it passes L2's
 * position only inside a loop that holds L2 and not L1, since it must come back by a
 * retreating edge.  So the positions to check are those after L1 up to L2, or up to the
 * end of the outermost such loop: a load in a loop is the one before the loop when the
 * loop writes nothing there, which is what hoisting it would give.  Loops are the ranges
 * of retreating edges; ranges that overlap without nesting become one loop (`lmerged`),
 * which is always checked whole.
 *
 * What may write: a store, unless it is to the same base at an offset range apart from
 * the load's (the same value plus different constants are different addresses), and
 * everything else that starts a new memory state above, which may write anything.
 */

/* a = base + off: a value or a slot, and the constant offsets added to it */
VPRIV void vw_baseoff(vwst *V, uint32_t a, vwaddr *A)
{
	uint64_t off = 0;
	int64_t k;

	a = vw_find(V, a);
	for (int hop = 0; hop < 16; hop++) {
		const vir_ins *in = &V->p[a].in;

		if (in->op != VO_PTRADD || V->p[a].blk == VW_NOBLK || !vw_kint(V, vw_find(V, in->b), &k))
			break;
		off += (uint64_t)k;
		a = vw_find(V, in->a);
	}
	A->slot = V->p[a].in.op == VO_SLOTADDR && V->p[a].blk == VW_NOBLK;
	if (A->slot) {
		off += (uint64_t)V->p[a].in.b | (uint64_t)V->p[a].in.c << 32;
		a = V->p[a].in.a;
	}
	A->base = a;
	A->off = (int64_t)off;
}

/* nothing at positions (lo, hi] may write the w bytes at A */
static bool vw_unwritten(vwst *V, vwsimp *S, uint32_t lo, uint32_t hi, const vwaddr *A, int64_t w)
{
	uint32_t i = 0, n = S->nwr;

	while (i < n) {	/* the first writer after lo */
		uint32_t m = i + (n - i) / 2;

		if (S->seq[S->wr[m]] <= lo)
			i = m + 1;
		else
			n = m;
	}
	for (n = 0; i < S->nwr && S->seq[S->wr[i]] <= hi; i++, n++) {
		const vir_ins *in = &V->p[S->wr[i]].in;
		vwaddr B;
		int64_t sw;

		if (n == VW_FARMAX || in->op != VO_STORE)
			return false;
		vw_baseoff(V, in->a, &B);
		sw = (int64_t)1 << VIF_W(in->fl);
		if (B.base != A->base || B.slot != A->slot || B.off <= -VW_FAROFF || B.off >= VW_FAROFF ||
		    !(B.off + sw <= A->off || A->off + w <= B.off))
			return false;
	}
	return true;
}

/* positions, writers and loops for vw_far (the blocks in reverse postorder) */
VPRIV void vw_far_init(ecx *e, vwsimp *S)
{
	vwst *V = e->vw;
	uint32_t n = V->norder, pos = 0, sp = 0, nl = 0, *stk, *iend;

	S->seq = calloc((size_t)S->n0 + 1, sizeof *S->seq);
	S->wr = malloc(((size_t)S->n0 + 1) * sizeof *S->wr);
	S->ri = malloc(((size_t)V->nb + 1) * sizeof *S->ri);
	S->bend = calloc((size_t)V->nb + 1, sizeof *S->bend);
	S->linner = malloc(((size_t)n + 1) * sizeof *S->linner);
	S->lstart = malloc(((size_t)n + 1) * sizeof *S->lstart);
	S->lend = malloc(((size_t)n + 1) * sizeof *S->lend);
	S->lpar = malloc(((size_t)n + 1) * sizeof *S->lpar);
	S->lmerged = calloc((size_t)n + 1, 1);
	stk = malloc(((size_t)n + 1) * sizeof *stk);
	iend = malloc(((size_t)n + 1) * sizeof *iend);
	if (!S->seq || !S->wr || !S->ri || !S->bend || !S->linner || !S->lstart || !S->lend || !S->lpar ||
	    !S->lmerged || !stk || !iend)
		vw_fail(e, VIRFAIL(out_of_memory));
	for (uint32_t b = 0; b < V->nb; b++)
		S->ri[b] = VW_NOBLK;
	for (uint32_t i = 0; i < n; i++)
		S->ri[V->order[i]] = i;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t b = V->order[i];

		for (uint32_t q = V->b[b].head;; q = V->p[q].next) {
			uint32_t x = q ? q : V->b[b].term, op, eff;

			if (!x)
				break;
			op = V->p[x].in.op;
			eff = vir_optab[op].eff;
			if (x < S->n0 && vw_find(V, x) == x && !vw_promdead(V, x)) {
				S->seq[x] = ++pos;
				if (eff & (EF_W | EF_C | EF_A) || op == VO_BARRIER || op == VO_VA_ARG || op == VO_VA_START)
					S->wr[S->nwr++] = x;
			}
			if (!q)
				break;
		}
		S->bend[b] = pos;
		/* the last index a retreating edge comes from */
		iend[i] = VW_NOBLK;
		for (uint32_t j = 0; j < V->b[b].npred; j++) {
			uint32_t r = S->ri[V->pr[V->b[b].pred + j]];

			if (r != VW_NOBLK && r >= i && (iend[i] == VW_NOBLK || r > iend[i]))
				iend[i] = r;
		}
	}
	for (uint32_t i = 0; i < n; i++) {
		while (sp && S->lend[stk[sp - 1]] < i)
			sp--;
		if (iend[i] != VW_NOBLK) {
			if (sp && S->lend[stk[sp - 1]] < iend[i]) {
				/* it leaves the open loops it ends after: one loop with each of them */
				for (uint32_t k = sp; k-- > 0 && S->lend[stk[k]] < iend[i];)
					S->lend[stk[k]] = iend[i], S->lmerged[stk[k]] = 1;
			} else {
				S->lstart[nl] = i, S->lend[nl] = iend[i];
				S->lpar[nl] = sp ? stk[sp - 1] : VW_NOBLK;
				stk[sp++] = nl++;
			}
		}
		S->linner[i] = sp ? stk[sp - 1] : VW_NOBLK;
	}
	free(stk), free(iend);
}

/* load q of the bytes at A: the table's entry for them, and whether its load (an earlier
 * one in a dominating block) still holds at q */
VPRIV vwhe *vw_far(vwst *V, vwsimp *S, uint32_t q, const vwaddr *A, bool *ok)
{
	const vir_ins *in = &V->p[q].in;
	vwhe key = { VO_LOAD | 0x100, in->ty, (in->fl & (3 | VIF_MS)) | (A->slot ? 0x100 : 0), A->base,
		     (uint32_t)(uint64_t)A->off, (uint32_t)((uint64_t)A->off >> 32), 0, 0 };
	vwhe *t = vw_hlook(S, &key);
	uint32_t l1 = t->v, hi = S->seq[q], ia;

	*ok = false;
	if (!l1) {
		*t = key;	/* (the caller sets v and logs it) */
		return t;
	}
	if (!S->seq[l1] || S->seq[l1] >= S->seq[q] || vw_find(V, l1) != l1 || V->p[l1].in.op != VO_LOAD)
		return t;
	ia = S->ri[V->p[l1].blk];
	for (uint32_t l = S->linner[S->ri[V->p[q].blk]]; l != VW_NOBLK; l = S->lpar[l]) {
		if (S->lstart[l] <= ia && !S->lmerged[l])
			break;	/* it holds l1 too: round it is through l1 again */
		if (S->bend[V->order[S->lend[l]]] > hi)
			hi = S->bend[V->order[S->lend[l]]];
	}
	*ok = vw_unwritten(V, S, S->seq[l1], hi, A, (int64_t)1 << VIF_W(in->fl));
	return t;
}
