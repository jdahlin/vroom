/* Placeholder parameters: trivial ones removed, dead ones dropped. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* a parameter all of whose operands are one value (or itself) is that value */
VPRIV bool virbuild_parameters_trivial_remove(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;
	bool any = false;

	for (uint32_t i = 0; i < V->block_order_count; i++) {
		virbuild_preir_block *k = &V->blocks[V->block_order[i]];

		for (uint32_t p = k->first_parameter; p; p = V->insns[p].next) {
			uint32_t same = 0;
			bool triv = true;

			if (V->forward[p] != p)
				continue;
			for (uint32_t j = 0; j < k->predecessor_count; j++) {
				uint32_t o = virbuild_value_resolve(V, V->extra[V->insns[p].insn.b + j]);

				if (o == p || o == same)
					continue;
				if (same) {
					triv = false;
					break;
				}
				same = o;
			}
			if (!triv)
				continue;
			V->forward[p] = same ? same : virbuild_undefined_value(e, V->insns[p].insn.type);
			any = true;
		}
	}
	return any;
}

/* parameters nothing reads but other parameters (Braun's phi cycles, a local of a loop
 * that every path writes before reading) go: they would hold a register through the loop */
static void virbuild_parameter_live_mark(virbuild_context *V, uint32_t *stk, uint32_t *n, uint32_t v)
{
	v = virbuild_value_resolve(V, v);
	if (V->insns[v].insn.op == VIR_OP_BPARAM && V->insns[v].block != VIRBUILD_BLOCK_NONE && !V->value_map[v]) {
		V->value_map[v] = 1;
		stk[(*n)++] = v;
	}
}

VPRIV void virbuild_parameters_dead_remove(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;
	uint32_t *stk, n = 0;

	VIRBUILD_ARRAY_GROW(V->value_map, 0, V->value_map_capacity, V->insn_count + 1);
	memset(V->value_map, 0, (size_t)V->insn_count * sizeof *V->value_map);
	stk = malloc((size_t)(V->insn_count + 1) * sizeof *stk);
	if (!stk)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i];

		for (uint32_t q = V->blocks[b].first_insn;; q = V->insns[q].next) {
			uint32_t x = q ? q : V->blocks[b].terminator;
			const vir_insn *in;
			const vir_opcode_info *oi;

			if (!x)
				break;
			in = &V->insns[x].insn;
			oi = &vir_opcode_table[in->op];
			if (virbuild_value_resolve(V, x) == x && in->op != VIR_OP_BR) {
				for (int k = 0; k < 3; k++)
					if (virbuild_operand_kind_is_value(oi->operand_kinds[k]) && !(in->op == VIR_OP_BRIF && k > 0))
						virbuild_parameter_live_mark(V, stk, &n, (&in->a)[k]);
				if (in->op == VIR_OP_CALL || in->op == VIR_OP_RET)
					for (uint32_t k = 0; k < V->extra[in->c]; k++)
						virbuild_parameter_live_mark(V, stk, &n, V->extra[in->c + 1 + k]);
			}
			if (!q)
				break;
		}
	}
	while (n) {	/* a live parameter's operands are live */
		uint32_t p = stk[--n], b = V->insns[p].block;

		for (uint32_t j = 0; j < V->blocks[b].predecessor_count; j++)
			virbuild_parameter_live_mark(V, stk, &n, V->extra[V->insns[p].insn.b + j]);
	}
	for (uint32_t i = 0; i < V->block_order_count; i++) {
		virbuild_preir_block *k = &V->blocks[V->block_order[i]];

		for (uint32_t p = k->first_parameter; p; p = V->insns[p].next)
			if (V->forward[p] == p && !V->value_map[p])
				V->forward[p] = virbuild_undefined_value(e, V->insns[p].insn.type);
	}
	free(stk);
}
