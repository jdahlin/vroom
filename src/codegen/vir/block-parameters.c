/* Placeholder parameters: trivial ones removed, dead ones dropped. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* p's one value other than itself, 0 none (its operands are all p), VIRBUILD_BLOCK_NONE two or more */
static uint32_t virbuild_parameter_trivial_value(virbuild_context *V, uint32_t p)
{
	uint32_t same = 0, npred = V->blocks[V->insns[p].block].predecessor_count;

	for (uint32_t j = 0; j < npred; j++) {
		uint32_t o = virbuild_value_resolve(V, V->extra[V->insns[p].insn.b + j]);

		if (o == p || o == same)
			continue;
		if (same)
			return VIRBUILD_BLOCK_NONE;
		same = o;
	}
	return same;
}

/* a parameter all of whose operands are one value (or itself) is that value.  A worklist
 * (Braun et al.'s tryRemoveTrivialPhi): removing one parameter rechecks only the
 * parameters that read it, so a nest of N loops costs O(N), not one sweep per level */
VPRIV bool virbuild_parameters_trivial_remove(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;
	uint32_t np = V->insn_count, nedge = 0, nw = 0;
	bool any = false;

	for (uint32_t i = 0; i < V->block_order_count; i++) {
		virbuild_preir_block *k = &V->blocks[V->block_order[i]];

		for (uint32_t p = k->first_parameter; p; p = V->insns[p].next)
			if (V->forward[p] == p)
				nedge += k->predecessor_count;
	}
	if (!nedge)
		return false;
	/* per value: its first user edge (0 none) and its last; per edge: the parameter reading
	 * the value, the next edge; the worklist and who is on it */
	uint32_t *head = calloc((size_t)np * 3 + (size_t)nedge * 2 + 2, sizeof *head);
	if (!head)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	uint32_t *tail = head + np, *work = tail + np, *user = work + np, *next = user + nedge + 1;
	uint8_t *queued = calloc(np, 1);
	if (!queued) {
		free(head);
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	}
	uint32_t ne = 0;

	for (uint32_t i = 0; i < V->block_order_count; i++) {
		virbuild_preir_block *k = &V->blocks[V->block_order[i]];

		for (uint32_t p = k->first_parameter; p; p = V->insns[p].next) {
			if (V->forward[p] != p)
				continue;
			for (uint32_t j = 0; j < k->predecessor_count; j++) {
				uint32_t o = virbuild_value_resolve(V, V->extra[V->insns[p].insn.b + j]);

				if (o == p || V->insns[o].insn.op != VIR_OP_BPARAM)
					continue;
				user[++ne] = p;	/* edges count from 1: 0 ends a list */
				if (tail[o])
					next[tail[o]] = ne;
				else
					head[o] = ne;
				tail[o] = ne;
			}
			queued[p] = 1;
			work[nw++] = p;
		}
	}
	while (nw) {
		uint32_t p = work[--nw], same;

		queued[p] = 0;
		if (V->forward[p] != p || (same = virbuild_parameter_trivial_value(V, p)) == VIRBUILD_BLOCK_NONE)
			continue;
		V->forward[p] = same ? same : virbuild_undefined_value(e, V->insns[p].insn.type);
		any = true;
		/* its readers may be trivial now, and they read same from now on: the live ones
		 * other than same move to same's list (removed readers would make it grow along
		 * a nest of loops, each level rescanning the levels inside it) */
		bool keep = same && same < np && V->insns[same].insn.op == VIR_OP_BPARAM;
		uint32_t kh = 0, kt = 0;

		for (uint32_t d = head[p], dn; d; d = dn) {
			uint32_t u = user[d];

			dn = next[d];
			if (V->forward[u] != u || u == same)
				continue;
			if (!queued[u]) {
				queued[u] = 1;
				work[nw++] = u;
			}
			if (!keep)
				continue;
			next[d] = 0;
			if (kt)
				next[kt] = d;
			else
				kh = d;
			kt = d;
		}
		head[p] = tail[p] = 0;
		if (kh) {
			if (tail[same])
				next[tail[same]] = kh;
			else
				head[same] = kh;
			tail[same] = kt;
		}
	}
	free(queued);
	free(head);
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
