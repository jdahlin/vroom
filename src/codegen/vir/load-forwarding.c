/* The simplifier's loads across blocks. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
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
VPRIV void virbuild_address_decompose(virbuild_context *V, uint32_t a, virbuild_address *A)
{
	uint64_t off = 0;
	int64_t k;

	a = virbuild_value_resolve(V, a);
	for (int hop = 0; hop < 16; hop++) {
		const vir_insn *in = &V->insns[a].insn;

		if (in->op != VIR_OP_PTRADD || V->insns[a].block == VIRBUILD_BLOCK_NONE || !virbuild_value_integer_constant(V, virbuild_value_resolve(V, in->b), &k))
			break;
		off += (uint64_t)k;
		a = virbuild_value_resolve(V, in->a);
	}
	A->slot = V->insns[a].insn.op == VIR_OP_SLOTADDR && V->insns[a].block == VIRBUILD_BLOCK_NONE;
	if (A->slot) {
		off += (uint64_t)V->insns[a].insn.b | (uint64_t)V->insns[a].insn.c << 32;
		a = V->insns[a].insn.a;
	}
	A->base = a;
	A->off = (int64_t)off;
}

/* nothing at positions (lo, hi] may write the w bytes at A */
static bool virbuild_bytes_are_unwritten(virbuild_context *V, virbuild_simplify_state *S, uint32_t lo, uint32_t hi, const virbuild_address *A, int64_t w)
{
	uint32_t i = 0, n = S->writer_count;

	while (i < n) {	/* the first writer after lo */
		uint32_t m = i + (n - i) / 2;

		if (S->positions[S->writers[m]] <= lo)
			i = m + 1;
		else
			n = m;
	}
	for (n = 0; i < S->writer_count && S->positions[S->writers[i]] <= hi; i++, n++) {
		const vir_insn *in = &V->insns[S->writers[i]].insn;
		virbuild_address B;
		int64_t sw;

		if (n == VIRBUILD_FAR_LOAD_WRITERS_MAX || in->op != VIR_OP_STORE)
			return false;
		virbuild_address_decompose(V, in->a, &B);
		sw = (int64_t)1 << VIR_INSN_FLAG_WIDTH_LOG2(in->flags);
		if (B.base != A->base || B.slot != A->slot || B.off <= -VIRBUILD_FAR_LOAD_OFFSET_MAX || B.off >= VIRBUILD_FAR_LOAD_OFFSET_MAX ||
		    !(B.off + sw <= A->off || A->off + w <= B.off))
			return false;
	}
	return true;
}

/* positions, writers and loops for virbuild_far_load_find (the blocks in reverse postorder) */
VPRIV void virbuild_far_load_init(codegen_context *e, virbuild_simplify_state *S)
{
	virbuild_context *V = e->vw;
	uint32_t n = V->block_order_count, pos = 0, sp = 0, nl = 0, *stk, *iend;

	S->positions = calloc((size_t)S->constant_start + 1, sizeof *S->positions);
	S->writers = malloc(((size_t)S->constant_start + 1) * sizeof *S->writers);
	S->reverse_postorder_index = malloc(((size_t)V->block_count + 1) * sizeof *S->reverse_postorder_index);
	S->block_end_positions = calloc((size_t)V->block_count + 1, sizeof *S->block_end_positions);
	S->loop_innermost = malloc(((size_t)n + 1) * sizeof *S->loop_innermost);
	S->loop_start = malloc(((size_t)n + 1) * sizeof *S->loop_start);
	S->loop_end = malloc(((size_t)n + 1) * sizeof *S->loop_end);
	S->loop_parent = malloc(((size_t)n + 1) * sizeof *S->loop_parent);
	S->loop_is_merged = calloc((size_t)n + 1, 1);
	stk = malloc(((size_t)n + 1) * sizeof *stk);
	iend = malloc(((size_t)n + 1) * sizeof *iend);
	if (!S->positions || !S->writers || !S->reverse_postorder_index || !S->block_end_positions || !S->loop_innermost || !S->loop_start || !S->loop_end || !S->loop_parent ||
	    !S->loop_is_merged || !stk || !iend)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	for (uint32_t b = 0; b < V->block_count; b++)
		S->reverse_postorder_index[b] = VIRBUILD_BLOCK_NONE;
	for (uint32_t i = 0; i < n; i++)
		S->reverse_postorder_index[V->block_order[i]] = i;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t b = V->block_order[i];

		for (uint32_t q = V->blocks[b].first_insn;; q = V->insns[q].next) {
			uint32_t x = q ? q : V->blocks[b].terminator, op, eff;

			if (!x)
				break;
			op = V->insns[x].insn.op;
			eff = vir_opcode_table[op].effects;
			if (x < S->constant_start && virbuild_value_resolve(V, x) == x && !virbuild_promote_store_is_dead(V, x)) {
				S->positions[x] = ++pos;
				if (eff & (VIR_EFFECT_W | VIR_EFFECT_C | VIR_EFFECT_A) || op == VIR_OP_BARRIER || op == VIR_OP_VA_ARG || op == VIR_OP_VA_START)
					S->writers[S->writer_count++] = x;
			}
			if (!q)
				break;
		}
		S->block_end_positions[b] = pos;
		/* the last index a retreating edge comes from */
		iend[i] = VIRBUILD_BLOCK_NONE;
		for (uint32_t j = 0; j < V->blocks[b].predecessor_count; j++) {
			uint32_t r = S->reverse_postorder_index[V->predecessors[V->blocks[b].predecessor_first + j]];

			if (r != VIRBUILD_BLOCK_NONE && r >= i && (iend[i] == VIRBUILD_BLOCK_NONE || r > iend[i]))
				iend[i] = r;
		}
	}
	for (uint32_t i = 0; i < n; i++) {
		while (sp && S->loop_end[stk[sp - 1]] < i)
			sp--;
		if (iend[i] != VIRBUILD_BLOCK_NONE) {
			if (sp && S->loop_end[stk[sp - 1]] < iend[i]) {
				/* it leaves the open loops it ends after: one loop with each of them */
				for (uint32_t k = sp; k-- > 0 && S->loop_end[stk[k]] < iend[i];)
					S->loop_end[stk[k]] = iend[i], S->loop_is_merged[stk[k]] = 1;
			} else {
				S->loop_start[nl] = i, S->loop_end[nl] = iend[i];
				S->loop_parent[nl] = sp ? stk[sp - 1] : VIRBUILD_BLOCK_NONE;
				stk[sp++] = nl++;
			}
		}
		S->loop_innermost[i] = sp ? stk[sp - 1] : VIRBUILD_BLOCK_NONE;
	}
	free(stk), free(iend);
}

/* load q of the bytes at A: the table's entry for them, and whether its load (an earlier
 * one in a dominating block) still holds at q */
VPRIV virbuild_value_entry *virbuild_far_load_find(virbuild_context *V, virbuild_simplify_state *S, uint32_t q, const virbuild_address *A, bool *ok)
{
	const vir_insn *in = &V->insns[q].insn;
	virbuild_value_entry key = { VIR_OP_LOAD | 0x100, in->type, (in->flags & (3 | VIR_INSN_FLAG_MS)) | (A->slot ? 0x100 : 0), A->base,
		     (uint32_t)(uint64_t)A->off, (uint32_t)((uint64_t)A->off >> 32), 0, 0 };
	virbuild_value_entry *t = virbuild_value_table_lookup(S, &key);
	uint32_t l1 = t->value, hi = S->positions[q], ia;

	*ok = false;
	if (!l1) {
		*t = key;	/* (the caller sets v and logs it) */
		return t;
	}
	if (!S->positions[l1] || S->positions[l1] >= S->positions[q] || virbuild_value_resolve(V, l1) != l1 || V->insns[l1].insn.op != VIR_OP_LOAD)
		return t;
	ia = S->reverse_postorder_index[V->insns[l1].block];
	for (uint32_t l = S->loop_innermost[S->reverse_postorder_index[V->insns[q].block]]; l != VIRBUILD_BLOCK_NONE; l = S->loop_parent[l]) {
		if (S->loop_start[l] <= ia && !S->loop_is_merged[l])
			break;	/* it holds l1 too: round it is through l1 again */
		if (S->block_end_positions[V->block_order[S->loop_end[l]]] > hi)
			hi = S->block_end_positions[V->block_order[S->loop_end[l]]];
	}
	*ok = virbuild_bytes_are_unwritten(V, S, S->positions[l1], hi, A, (int64_t)1 << VIR_INSN_FLAG_WIDTH_LOG2(in->flags));
	return t;
}
