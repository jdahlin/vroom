/* Placeholder parameters: trivial ones removed, dead ones dropped. */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* a parameter all of whose operands are one value (or itself) is that value */
VPRIV bool virbuild_parameters_trivial_remove(ecx *e)
{
	virbuild_context *V = e->vw;
	bool any = false;

	for (uint32_t i = 0; i < V->norder; i++) {
		virbuild_preir_block *k = &V->b[V->order[i]];

		for (uint32_t p = k->phead; p; p = V->p[p].next) {
			uint32_t same = 0;
			bool triv = true;

			if (V->fwd[p] != p)
				continue;
			for (uint32_t j = 0; j < k->npred; j++) {
				uint32_t o = virbuild_value_resolve(V, V->x[V->p[p].in.b + j]);

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
			V->fwd[p] = same ? same : virbuild_undefined_value(e, V->p[p].in.type);
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
	if (V->p[v].in.op == VIR_OP_BPARAM && V->p[v].blk != VIRBUILD_BLOCK_NONE && !V->map[v]) {
		V->map[v] = 1;
		stk[(*n)++] = v;
	}
}

VPRIV void virbuild_parameters_dead_remove(ecx *e)
{
	virbuild_context *V = e->vw;
	uint32_t *stk, n = 0;

	VIRBUILD_ARRAY_GROW(V->map, 0, V->cmap, V->np + 1);
	memset(V->map, 0, (size_t)V->np * sizeof *V->map);
	stk = malloc((size_t)(V->np + 1) * sizeof *stk);
	if (!stk)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t q = V->b[b].head;; q = V->p[q].next) {
			uint32_t x = q ? q : V->b[b].term;
			const vir_insn *in;
			const vir_opcode_info *oi;

			if (!x)
				break;
			in = &V->p[x].in;
			oi = &vir_opcode_table[in->op];
			if (virbuild_value_resolve(V, x) == x && in->op != VIR_OP_BR) {
				for (int k = 0; k < 3; k++)
					if (virbuild_operand_kind_is_value(oi->operand_kinds[k]) && !(in->op == VIR_OP_BRIF && k > 0))
						virbuild_parameter_live_mark(V, stk, &n, (&in->a)[k]);
				if (in->op == VIR_OP_CALL || in->op == VIR_OP_RET)
					for (uint32_t k = 0; k < V->x[in->c]; k++)
						virbuild_parameter_live_mark(V, stk, &n, V->x[in->c + 1 + k]);
			}
			if (!q)
				break;
		}
	}
	while (n) {	/* a live parameter's operands are live */
		uint32_t p = stk[--n], b = V->p[p].blk;

		for (uint32_t j = 0; j < V->b[b].npred; j++)
			virbuild_parameter_live_mark(V, stk, &n, V->x[V->p[p].in.b + j]);
	}
	for (uint32_t i = 0; i < V->norder; i++) {
		virbuild_preir_block *k = &V->b[V->order[i]];

		for (uint32_t p = k->phead; p; p = V->p[p].next)
			if (V->fwd[p] == p && !V->map[p])
				V->fwd[p] = virbuild_undefined_value(e, V->p[p].in.type);
	}
	free(stk);
}
