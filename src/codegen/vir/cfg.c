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
	uint32_t t = V->blocks[b].terminator;

	if (!t)
		return 0;
	switch (V->insns[t].insn.op) {
	case VIR_OP_BR: return 1;
	case VIR_OP_BRIF: return 2;
	case VIR_OP_SWITCH: return 1 + V->extra[V->insns[t].insn.c];
	default: return 0;
	}
}

VPRIV uint32_t *virbuild_block_successor_pointer(virbuild_context *V, uint32_t b, uint32_t k)
{
	vir_insn *in = &V->insns[V->blocks[b].terminator].insn;

	switch (in->op) {
	case VIR_OP_BR: return &in->a;
	case VIR_OP_BRIF: return k ? &in->c : &in->b;
	default: {
		uint32_t st = V->insns[in->a].insn.type == VIR_TYPE_I64 ? 5 : 3;

		return k ? &V->extra[in->c + 1 + (k - 1) * st + st - 1] : &in->b;
	}
	}
}

/* a block with nothing in it that only jumps on: its predecessors jump on directly */
VPRIV void virbuild_empty_blocks_thread(ecx *e)
{
	virbuild_context *V = e->vw;

	for (uint32_t b = 0; b < V->block_count; b++) {
		uint32_t n = virbuild_block_successor_count(V, b);

		for (uint32_t k = 0; k < n; k++) {
			uint32_t *t = virbuild_block_successor_pointer(V, b, k);

			for (int hop = 0; hop < 64; hop++) {
				virbuild_preir_block *q = &V->blocks[*t];

				if (*t == 0 || q->first_insn || q->first_parameter || q->has_definition || !q->terminator || V->insns[q->terminator].insn.op != VIR_OP_BR || V->insns[q->terminator].insn.a == *t)
					break;
				*t = V->insns[q->terminator].insn.a;
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

	VIRBUILD_ARRAY_GROW(V->block_map, 0, V->block_map_capacity, 2 * V->block_count + 2);
	stk = V->block_map, it = V->block_map + V->block_count + 1;
	VIRBUILD_ARRAY_GROW(V->block_order, 0, V->block_order_capacity, V->block_count + 1);
	for (uint32_t b = 0; b < V->block_count; b++)
		V->blocks[b].is_reachable = 0;
	V->blocks[0].is_reachable = 1;
	stk[sp] = 0, it[sp++] = 0;
	while (sp) {
		uint32_t b = stk[sp - 1], k = it[sp - 1];

		if (k < virbuild_block_successor_count(V, b)) {
			uint32_t t = *virbuild_block_successor_pointer(V, b, k);

			it[sp - 1]++;
			if (!V->blocks[t].is_reachable) {
				V->blocks[t].is_reachable = 1;
				stk[sp] = t, it[sp++] = 0;
			}
			continue;
		}
		V->block_order[n++] = b;	/* postorder */
		sp--;
	}
	for (uint32_t i = 0; i < n / 2; i++) {
		uint32_t t = V->block_order[i];

		V->block_order[i] = V->block_order[n - 1 - i], V->block_order[n - 1 - i] = t;
	}
	V->block_order_count = n;
}

/* the distinct reachable predecessors of every block */
VPRIV void virbuild_block_predecessors_compute(ecx *e)
{
	virbuild_context *V = e->vw;

	for (uint32_t b = 0; b < V->block_count; b++)
		V->blocks[b].predecessor_count = 0;
	V->predecessor_count = 0;
	/* count (with repeats), then fill and drop repeats */
	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i], n = virbuild_block_successor_count(V, b);

		for (uint32_t k = 0; k < n; k++)
			V->blocks[*virbuild_block_successor_pointer(V, b, k)].predecessor_count++;
	}
	for (uint32_t b = 0; b < V->block_count; b++) {
		V->blocks[b].predecessor_first = V->predecessor_count;
		V->predecessor_count += V->blocks[b].predecessor_count;
		V->blocks[b].predecessor_count = 0;
	}
	VIRBUILD_ARRAY_GROW(V->predecessors, 0, V->predecessor_capacity, V->predecessor_count + 1);
	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i], n = virbuild_block_successor_count(V, b);

		for (uint32_t k = 0; k < n; k++) {
			virbuild_preir_block *t = &V->blocks[*virbuild_block_successor_pointer(V, b, k)];
			bool dup = false;

			for (uint32_t j = 0; j < t->predecessor_count; j++)
				dup |= V->predecessors[t->predecessor_first + j] == b;
			if (!dup)
				V->predecessors[t->predecessor_first + t->predecessor_count++] = b;
		}
	}
}

/* immediate dominators (Cooper, Harvey, Kennedy) and the tree's children, in reverse postorder */
VPRIV void virbuild_block_dominators_compute(ecx *e, virbuild_simplify_state *S)
{
	virbuild_context *V = e->vw;
	uint32_t n = V->block_order_count, *ri = malloc(((size_t)V->block_count + 1) * sizeof *ri);
	bool changed = true;

	S->immediate_dominators = malloc(((size_t)V->block_count + 1) * sizeof *S->immediate_dominators);
	S->dominator_first_child = malloc(((size_t)V->block_count + 1) * sizeof *S->dominator_first_child);
	S->dominator_next_sibling = malloc(((size_t)V->block_count + 1) * sizeof *S->dominator_next_sibling);
	if (!ri || !S->immediate_dominators || !S->dominator_first_child || !S->dominator_next_sibling)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	for (uint32_t b = 0; b < V->block_count; b++)
		ri[b] = VIRBUILD_BLOCK_NONE, S->immediate_dominators[b] = VIRBUILD_BLOCK_NONE, S->dominator_first_child[b] = VIRBUILD_BLOCK_NONE, S->dominator_next_sibling[b] = VIRBUILD_BLOCK_NONE;
	for (uint32_t i = 0; i < n; i++)
		ri[V->block_order[i]] = i;
	S->immediate_dominators[V->block_order[0]] = V->block_order[0];
	while (changed) {
		changed = false;
		for (uint32_t i = 1; i < n; i++) {
			uint32_t b = V->block_order[i], d = VIRBUILD_BLOCK_NONE;

			for (uint32_t j = 0; j < V->blocks[b].predecessor_count; j++) {
				uint32_t p = V->predecessors[V->blocks[b].predecessor_first + j];

				if (ri[p] == VIRBUILD_BLOCK_NONE || S->immediate_dominators[p] == VIRBUILD_BLOCK_NONE)
					continue;
				if (d == VIRBUILD_BLOCK_NONE) {
					d = p;
					continue;
				}
				while (p != d) {
					while (ri[p] > ri[d])
						p = S->immediate_dominators[p];
					while (ri[d] > ri[p])
						d = S->immediate_dominators[d];
				}
			}
			if (d != S->immediate_dominators[b]) {
				S->immediate_dominators[b] = d;
				changed = true;
			}
		}
	}
	for (uint32_t i = n; i-- > 1;) {	/* (pushed in reverse: kept in order) */
		uint32_t b = V->block_order[i], d = S->immediate_dominators[b];

		S->dominator_next_sibling[b] = S->dominator_first_child[d];
		S->dominator_first_child[d] = b;
	}
	free(ri);
}

VPRIV uint32_t virbuild_block_predecessor_index(const virbuild_context *V, uint32_t j, uint32_t b)
{
	for (uint32_t i = 0; i < V->blocks[j].predecessor_count; i++)
		if (V->predecessors[V->blocks[j].predecessor_first + i] == b)
			return i;
	return VIRBUILD_BLOCK_NONE;
}

/* the edge b -> t needs a block of its own: t has parameters and b does not end in br */
VPRIV void virbuild_critical_edges_split(ecx *e)
{
	virbuild_context *V = e->vw;
	uint32_t n0 = V->block_order_count, nb0 = V->block_count;	/* blocks from nb0 on are this pass's edge blocks */

	for (uint32_t i = 0; i < n0; i++) {
		uint32_t b = V->block_order[i], ns = virbuild_block_successor_count(V, b);

		if (V->insns[V->blocks[b].terminator].insn.op == VIR_OP_BR)
			continue;
		for (uint32_t k = 0; k < ns; k++) {
			uint32_t t = *virbuild_block_successor_pointer(V, b, k), live = 0, eb = VIRBUILD_BLOCK_NONE;

			for (uint32_t p = V->blocks[t].first_parameter; p; p = V->insns[p].next)
				live += V->forward[p] == p;
			if (!live)
				continue;
			/* one edge block per (b, t): t's predecessor b becomes it */
			for (uint32_t j = 0; j < V->blocks[t].predecessor_count; j++)
				if (V->predecessors[V->blocks[t].predecessor_first + j] == b) {
					eb = virbuild_block_new(e);
					V->blocks[eb].terminator = virbuild_preir_insn_add(e, VIR_OP_BR, VIR_TYPE_VOID, 0, t, 0, 0, eb);
					V->blocks[eb].is_reachable = 1;
					VIRBUILD_ARRAY_GROW(V->predecessors, V->predecessor_count, V->predecessor_capacity, 1);
					V->blocks[eb].predecessor_first = V->predecessor_count;
					V->predecessors[V->predecessor_count++] = b;
					V->blocks[eb].predecessor_count = 1;
					V->predecessors[V->blocks[t].predecessor_first + j] = eb;
					VIRBUILD_ARRAY_GROW(V->block_order, V->block_order_count, V->block_order_capacity, 1);
					V->block_order[V->block_order_count++] = eb;
					break;
				}
			if (eb == VIRBUILD_BLOCK_NONE) {	/* already split: the edge block whose br goes to t
						 * (not a block of the function that only branches there:
						 * switch cases 8, 9 -> t and 12 -> { op = 10; br t }) */
				for (uint32_t j = 0; j < V->blocks[t].predecessor_count; j++) {
					uint32_t q = V->predecessors[V->blocks[t].predecessor_first + j];

					if (q >= nb0 && V->blocks[q].predecessor_count == 1 && V->predecessors[V->blocks[q].predecessor_first] == b && !V->blocks[q].first_insn &&
					    V->insns[V->blocks[q].terminator].insn.op == VIR_OP_BR && V->insns[V->blocks[q].terminator].insn.a == t)
						eb = q;
				}
			}
			*virbuild_block_successor_pointer(V, b, k) = eb;
		}
	}
	if (V->block_order_count == n0)
		return;
	/* the edge blocks right after the block they leave (layout: a fall-through, not a jump back) */
	{
		uint32_t *o = malloc((size_t)V->block_order_count * sizeof *o), m = 0;

		if (!o)
			virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
		for (uint32_t i = 0; i < n0; i++) {
			uint32_t b = V->block_order[i];

			o[m++] = b;
			for (uint32_t j = n0; j < V->block_order_count; j++)
				if (V->predecessors[V->blocks[V->block_order[j]].predecessor_first] == b)
					o[m++] = V->block_order[j];
		}
		memcpy(V->block_order, o, (size_t)m * sizeof *o);
		free(o);
	}
}
