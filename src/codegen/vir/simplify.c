/* The simplifier's driver and the pass that gives each access its own address. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

/*
 * base + constant as an address is the load's or store's own operand on the CPU, but the
 * lowering folds only a ptradd that its one user follows directly; numbering gives a
 * block's accesses to one field a shared ptradd, which then costs an instruction and a
 * register.  So each such access gets its own copy just before it (the shared one dies
 * when nothing else reads it).  Only offsets every CPU here encodes: -256..255, or a
 * multiple of the access size up to 4095 of them.
 */
static void virbuild_address_offsets_fold(codegen_context *e, virbuild_simplify_state *S)
{
	virbuild_context *V = e->vir_builder;

	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i];

		for (uint32_t q = V->blocks[b].first_insn, pq = 0; q; pq = q, q = V->insns[q].next) {
			vir_insn in = V->insns[q].insn;
			uint32_t a, n, w = 1u << VIR_INSN_FLAG_WIDTH_LOG2(in.flags);
			int64_t k;

			if ((in.op != VIR_OP_LOAD && in.op != VIR_OP_STORE) || q >= S->constant_start || virbuild_value_resolve(V, q) != q || virbuild_promote_store_is_dead(V, q) ||
			    (in.op == VIR_OP_LOAD && !S->use_counts[q] && !(in.flags & VIR_INSN_FLAG_VOL)))
				continue;
			a = virbuild_value_resolve(V, in.a);
			if (a >= S->constant_start || V->insns[a].insn.op != VIR_OP_PTRADD || V->insns[a].block == VIRBUILD_BLOCK_NONE ||
			    !virbuild_value_integer_constant(V, virbuild_value_resolve(V, V->insns[a].insn.b), &k) ||
			    !((k >= -256 && k <= 255) || (k > 0 && !(k % w) && k / w <= 4095)))
				continue;
			if (a == pq && S->use_counts[a] == 1)
				continue;	/* (its own already) */
			n = virbuild_preir_insn_add(e, VIR_OP_PTRADD, VIR_TYPE_PTR, V->insns[a].insn.flags, virbuild_value_resolve(V, V->insns[a].insn.a), virbuild_value_resolve(V, V->insns[a].insn.b), 0, b);
			V->insns[n].location = V->insns[q].location;
			V->insns[n].next = q;
			if (pq)
				V->insns[pq].next = n;
			else
				V->blocks[b].first_insn = n;
			V->insns[q].insn.a = n;
			virbuild_use_count_decrement(V, S, a), virbuild_use_count_increment(V, S, V->insns[n].insn.a);
		}
	}
}

VPRIV void virbuild_function_simplify(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;
	virbuild_simplify_state S = { 0 };
	uint32_t *stk, ns = 0;

	S.constant_start = V->insn_count;
	S.use_counts = calloc(2 * ((size_t)S.constant_start + 1), sizeof *S.use_counts);
	stk = malloc(((size_t)S.constant_start + 1) * sizeof *stk);
	{
		uint32_t kc = 16;

		for (uint32_t i = 0; i < V->insn_count; i++)
			S.constant_count += V->insns[i].block == VIRBUILD_BLOCK_NONE;
		while (kc < 2 * (V->insn_count + 64))	/* (and the constants the pass makes) */
			kc *= 2;
		S.constant_table = calloc(kc, sizeof *S.constant_table);
		S.constant_table_mask = kc - 1;
	}
	if (!S.use_counts || !stk || !S.constant_table)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	uint32_t *pointer_copies = S.use_counts + S.constant_start + 1;
	/* uses */
	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i];

		for (uint32_t p = V->blocks[b].first_parameter; p; p = V->insns[p].next)
			if (V->forward[p] == p)
				VIRBUILD_FOR_EACH_OPERAND(V, p, o, virbuild_use_count_increment(V, &S, virbuild_value_resolve(V, *o)));
		for (uint32_t q = V->blocks[b].first_insn;; q = V->insns[q].next) {
			uint32_t x = q ? q : V->blocks[b].terminator;

			if (!x)
				break;
			if (virbuild_value_resolve(V, x) == x &&
			    !virbuild_promote_store_is_dead(V, x)) {
				VIRBUILD_FOR_EACH_OPERAND(V, x, o, virbuild_use_count_increment(V, &S, virbuild_value_resolve(V, *o)));
				if (V->insns[x].insn.op == VIR_OP_I2P) pointer_copies[virbuild_value_resolve(V, V->insns[x].insn.a)]++;
			}
			if (!q)
				break;
		}
	}
	/* Pointer-only views need no separately allocated integer carrier. */
	for (uint32_t q = 0; q < S.constant_start; q++) {
		vir_insn *in = &V->insns[q].insn;
		if (in->op == VIR_OP_LOAD && in->type == VIR_TYPE_I64 && !(in->flags & (VIR_INSN_FLAG_VOL | VIR_INSN_FLAG_MS)) &&
		    VIR_INSN_FLAG_WIDTH_LOG2(in->flags) == 3 && pointer_copies[q] && pointer_copies[q] == S.use_counts[q]) in->type = VIR_TYPE_PTR;
	}
	/* rewriting and numbering over the dominator tree */
	virbuild_block_dominators_compute(e, &S);
	virbuild_far_load_init(e, &S);
	{
		uint32_t cap = 16, top = 0, *bst = malloc((size_t)(V->block_count + 1) * 2 * sizeof *bst);

		while (cap < 2 * (V->insn_count - S.constant_count) + 2)
			cap *= 2;
		S.value_table = calloc(cap, sizeof *S.value_table);
		S.value_table_mask = cap - 1;
		S.table_log = malloc((2 * (size_t)V->insn_count + 2) * sizeof *S.table_log);	/* (a store or a load adds two) */
		S.end_generations = malloc(((size_t)V->block_count + 1) * sizeof *S.end_generations);
		if (!S.value_table || !S.table_log || !bst || !S.end_generations)
			virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
		bst[top++] = V->block_order[0], bst[top++] = 0;
		while (top) {
			uint32_t ph = bst[--top], b = bst[--top];

			if (ph) {	/* leaving b: its entries go */
				while (S.table_log_count > ph - 1) {
					S.table_log_count--;
					S.value_table[S.table_log[S.table_log_count]].value = 0;
				}
				continue;
			}
			bst[top++] = b, bst[top++] = S.table_log_count + 1;
			S.memory_generation = V->blocks[b].predecessor_count == 1 ? S.end_generations[V->predecessors[V->blocks[b].predecessor_first]] : ++S.block_generations;
			S.block = ++S.block_generations;
			virbuild_block_values_number(e, &S, b);
			S.end_generations[b] = S.memory_generation;
			for (uint32_t c = S.dominator_first_child[b]; c != VIRBUILD_BLOCK_NONE; c = S.dominator_next_sibling[c])
				bst[top++] = c, bst[top++] = 0;
		}
		free(bst);
	}
	virbuild_address_offsets_fold(e, &S);
	/* what nothing reads */
	for (uint32_t i = 0; i < V->block_order_count; i++)
		for (uint32_t q = V->blocks[V->block_order[i]].first_insn; q; q = V->insns[q].next)
			if (q < S.constant_start && virbuild_value_resolve(V, q) == q && !S.use_counts[q])
				stk[ns++] = q;
	while (ns) {
		uint32_t q = stk[--ns], op = V->insns[q].insn.op, eff = vir_opcode_table[op].effects;

		if (virbuild_value_resolve(V, q) != q || S.use_counts[q] || V->insns[q].block == VIRBUILD_BLOCK_NONE)
			continue;
		if (op == VIR_OP_LOAD ? (V->insns[q].insn.flags & VIR_INSN_FLAG_VOL) != 0 :
		    ((eff & ~(VIR_EFFECT_UF | VIR_EFFECT_F)) != VIR_EFFECT_P || op == VIR_OP_BPARAM || op == VIR_OP_PARAM))	/* (FP status flags are not kept: no FENV_ACCESS) */
			continue;
		VIRBUILD_FOR_EACH_OPERAND(V, q, o, {
			uint32_t x = virbuild_value_resolve(V, *o);

			virbuild_use_count_decrement(V, &S, x);
			if (x < S.constant_start && V->insns[x].block != VIRBUILD_BLOCK_NONE && !S.use_counts[x] && V->insns[x].insn.op != VIR_OP_BPARAM)
				stk[ns++] = x;
		});
		V->forward[q] = virbuild_simplify_undefined_value(e, &S, V->insns[q].insn.type);
	}
	free(S.value_table), free(S.table_log), free(S.end_generations), free(S.constant_table);
	free(S.immediate_dominators), free(S.dominator_first_child), free(S.dominator_next_sibling);
	free(S.positions), free(S.writers), free(S.reverse_postorder_index), free(S.block_end_positions), free(S.loop_innermost), free(S.loop_start), free(S.loop_end), free(S.loop_parent), free(S.loop_is_merged);
	free(S.use_counts);
	free(stk);
}
