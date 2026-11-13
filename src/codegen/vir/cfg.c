/* The pre-IR's control flow graph: successors, block order, predecessors, dominators, edge splitting. */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

/* ---------------- sealing and emission ---------------- */


/* the successors of pre-block b, in operand order, through its terminator (n words at *out) */
VPRIV uint32_t virbuild_block_successor_count(virbuild_context *V, uint32_t b)
{
	uint32_t t = V->b[b].term;

	if (!t)
		return 0;
	switch (V->p[t].in.op) {
	case VIR_OP_BR: return 1;
	case VIR_OP_BRIF: return 2;
	case VIR_OP_SWITCH: return 1 + V->x[V->p[t].in.c];
	default: return 0;
	}
}

VPRIV uint32_t *virbuild_block_successor_pointer(virbuild_context *V, uint32_t b, uint32_t k)
{
	vir_insn *in = &V->p[V->b[b].term].in;

	switch (in->op) {
	case VIR_OP_BR: return &in->a;
	case VIR_OP_BRIF: return k ? &in->c : &in->b;
	default: {
		uint32_t st = V->p[in->a].in.type == VIR_TYPE_I64 ? 5 : 3;

		return k ? &V->x[in->c + 1 + (k - 1) * st + st - 1] : &in->b;
	}
	}
}

/* a block with nothing in it that only jumps on: its predecessors jump on directly */
VPRIV void virbuild_empty_blocks_thread(ecx *e)
{
	virbuild_context *V = e->vw;

	for (uint32_t b = 0; b < V->nb; b++) {
		uint32_t n = virbuild_block_successor_count(V, b);

		for (uint32_t k = 0; k < n; k++) {
			uint32_t *t = virbuild_block_successor_pointer(V, b, k);

			for (int hop = 0; hop < 64; hop++) {
				virbuild_preir_block *q = &V->b[*t];

				if (*t == 0 || q->head || q->phead || q->def || !q->term || V->p[q->term].in.op != VIR_OP_BR || V->p[q->term].in.a == *t)
					break;
				*t = V->p[q->term].in.a;
			}
		}
	}
}

/* reachable blocks in reverse postorder (V->order) */
VPRIV void virbuild_block_order_compute(ecx *e)
{
	virbuild_context *V = e->vw;
	uint32_t n = 0, sp = 0;
	uint32_t *stk, *it;

	GROW(V->bmap, 0, V->cbmap, 2 * V->nb + 2);
	stk = V->bmap, it = V->bmap + V->nb + 1;
	GROW(V->order, 0, V->corder, V->nb + 1);
	for (uint32_t b = 0; b < V->nb; b++)
		V->b[b].reach = 0;
	V->b[0].reach = 1;
	stk[sp] = 0, it[sp++] = 0;
	while (sp) {
		uint32_t b = stk[sp - 1], k = it[sp - 1];

		if (k < virbuild_block_successor_count(V, b)) {
			uint32_t t = *virbuild_block_successor_pointer(V, b, k);

			it[sp - 1]++;
			if (!V->b[t].reach) {
				V->b[t].reach = 1;
				stk[sp] = t, it[sp++] = 0;
			}
			continue;
		}
		V->order[n++] = b;	/* postorder */
		sp--;
	}
	for (uint32_t i = 0; i < n / 2; i++) {
		uint32_t t = V->order[i];

		V->order[i] = V->order[n - 1 - i], V->order[n - 1 - i] = t;
	}
	V->norder = n;
}

/* the distinct reachable predecessors of every block */
VPRIV void virbuild_block_predecessors_compute(ecx *e)
{
	virbuild_context *V = e->vw;

	for (uint32_t b = 0; b < V->nb; b++)
		V->b[b].npred = 0;
	V->npr = 0;
	/* count (with repeats), then fill and drop repeats */
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i], n = virbuild_block_successor_count(V, b);

		for (uint32_t k = 0; k < n; k++)
			V->b[*virbuild_block_successor_pointer(V, b, k)].npred++;
	}
	for (uint32_t b = 0; b < V->nb; b++) {
		V->b[b].pred = V->npr;
		V->npr += V->b[b].npred;
		V->b[b].npred = 0;
	}
	GROW(V->pr, 0, V->cpr, V->npr + 1);
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i], n = virbuild_block_successor_count(V, b);

		for (uint32_t k = 0; k < n; k++) {
			virbuild_preir_block *t = &V->b[*virbuild_block_successor_pointer(V, b, k)];
			bool dup = false;

			for (uint32_t j = 0; j < t->npred; j++)
				dup |= V->pr[t->pred + j] == b;
			if (!dup)
				V->pr[t->pred + t->npred++] = b;
		}
	}
}

/* immediate dominators (Cooper, Harvey, Kennedy) and the tree's children, in reverse postorder */
VPRIV void virbuild_block_dominators_compute(ecx *e, virbuild_simplify_state *S)
{
	virbuild_context *V = e->vw;
	uint32_t n = V->norder, *ri = malloc(((size_t)V->nb + 1) * sizeof *ri);
	bool changed = true;

	S->idom = malloc(((size_t)V->nb + 1) * sizeof *S->idom);
	S->dkid = malloc(((size_t)V->nb + 1) * sizeof *S->dkid);
	S->dnext = malloc(((size_t)V->nb + 1) * sizeof *S->dnext);
	if (!ri || !S->idom || !S->dkid || !S->dnext)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	for (uint32_t b = 0; b < V->nb; b++)
		ri[b] = VIRBUILD_BLOCK_NONE, S->idom[b] = VIRBUILD_BLOCK_NONE, S->dkid[b] = VIRBUILD_BLOCK_NONE, S->dnext[b] = VIRBUILD_BLOCK_NONE;
	for (uint32_t i = 0; i < n; i++)
		ri[V->order[i]] = i;
	S->idom[V->order[0]] = V->order[0];
	while (changed) {
		changed = false;
		for (uint32_t i = 1; i < n; i++) {
			uint32_t b = V->order[i], d = VIRBUILD_BLOCK_NONE;

			for (uint32_t j = 0; j < V->b[b].npred; j++) {
				uint32_t p = V->pr[V->b[b].pred + j];

				if (ri[p] == VIRBUILD_BLOCK_NONE || S->idom[p] == VIRBUILD_BLOCK_NONE)
					continue;
				if (d == VIRBUILD_BLOCK_NONE) {
					d = p;
					continue;
				}
				while (p != d) {
					while (ri[p] > ri[d])
						p = S->idom[p];
					while (ri[d] > ri[p])
						d = S->idom[d];
				}
			}
			if (d != S->idom[b]) {
				S->idom[b] = d;
				changed = true;
			}
		}
	}
	for (uint32_t i = n; i-- > 1;) {	/* (pushed in reverse: kept in order) */
		uint32_t b = V->order[i], d = S->idom[b];

		S->dnext[b] = S->dkid[d];
		S->dkid[d] = b;
	}
	free(ri);
}

VPRIV uint32_t virbuild_block_predecessor_index(const virbuild_context *V, uint32_t j, uint32_t b)
{
	for (uint32_t i = 0; i < V->b[j].npred; i++)
		if (V->pr[V->b[j].pred + i] == b)
			return i;
	return VIRBUILD_BLOCK_NONE;
}

/* the edge b -> t needs a block of its own: t has parameters and b does not end in br */
VPRIV void virbuild_critical_edges_split(ecx *e)
{
	virbuild_context *V = e->vw;
	uint32_t n0 = V->norder, nb0 = V->nb;	/* blocks from nb0 on are this pass's edge blocks */

	for (uint32_t i = 0; i < n0; i++) {
		uint32_t b = V->order[i], ns = virbuild_block_successor_count(V, b);

		if (V->p[V->b[b].term].in.op == VIR_OP_BR)
			continue;
		for (uint32_t k = 0; k < ns; k++) {
			uint32_t t = *virbuild_block_successor_pointer(V, b, k), live = 0, eb = VIRBUILD_BLOCK_NONE;

			for (uint32_t p = V->b[t].phead; p; p = V->p[p].next)
				live += V->fwd[p] == p;
			if (!live)
				continue;
			/* one edge block per (b, t): t's predecessor b becomes it */
			for (uint32_t j = 0; j < V->b[t].npred; j++)
				if (V->pr[V->b[t].pred + j] == b) {
					eb = virbuild_block_new(e);
					V->b[eb].term = virbuild_preir_insn_add(e, VIR_OP_BR, VIR_TYPE_VOID, 0, t, 0, 0, eb);
					V->b[eb].reach = 1;
					GROW(V->pr, V->npr, V->cpr, 1);
					V->b[eb].pred = V->npr;
					V->pr[V->npr++] = b;
					V->b[eb].npred = 1;
					V->pr[V->b[t].pred + j] = eb;
					GROW(V->order, V->norder, V->corder, 1);
					V->order[V->norder++] = eb;
					break;
				}
			if (eb == VIRBUILD_BLOCK_NONE) {	/* already split: the edge block whose br goes to t
						 * (not a block of the function that only branches there:
						 * switch cases 8, 9 -> t and 12 -> { op = 10; br t }) */
				for (uint32_t j = 0; j < V->b[t].npred; j++) {
					uint32_t q = V->pr[V->b[t].pred + j];

					if (q >= nb0 && V->b[q].npred == 1 && V->pr[V->b[q].pred] == b && !V->b[q].head &&
					    V->p[V->b[q].term].in.op == VIR_OP_BR && V->p[V->b[q].term].in.a == t)
						eb = q;
				}
			}
			*virbuild_block_successor_pointer(V, b, k) = eb;
		}
	}
	if (V->norder == n0)
		return;
	/* the edge blocks right after the block they leave (layout: a fall-through, not a jump back) */
	{
		uint32_t *o = malloc((size_t)V->norder * sizeof *o), m = 0;

		if (!o)
			virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
		for (uint32_t i = 0; i < n0; i++) {
			uint32_t b = V->order[i];

			o[m++] = b;
			for (uint32_t j = n0; j < V->norder; j++)
				if (V->pr[V->b[V->order[j]].pred] == b)
					o[m++] = V->order[j];
		}
		memcpy(V->order, o, (size_t)m * sizeof *o);
		free(o);
	}
}
