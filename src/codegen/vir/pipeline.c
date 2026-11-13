/* The builder's pipeline: the pass table run in order (pass-table.c), sealing, the dead-slot scan and the emission into VIR. */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"
#include "vectorize.h"
#include "pass-table.h"

/* seal: every placeholder gets its operands, then trivial ones go */
VPRIV void virbuild_blocks_seal(ecx *e)
{
	virbuild_context *V = e->vw;

	V->sealing = true;
	VIRBUILD_ARRAY_GROW(V->forward, 0, V->forward_capacity, V->insn_count + 1);
	for (uint32_t i = 0; i < V->insn_count; i++)
		V->forward[i] = i;
	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i];

		for (uint32_t p = V->blocks[b].first_parameter; p; p = V->insns[p].next)
			if (V->insns[p].insn.b == VIRBUILD_BLOCK_NONE)
				virbuild_parameter_operands_fill(e, p, b);
	}
}

/* stores to a slot that is not exposed and that nothing reads are dead */
VPRIV void virbuild_slot_uses_compute(ecx *e)
{
	virbuild_context *V = e->vw;

	for (uint32_t s = 0; s < V->slot_count; s++)
		V->slots[s].used = 0, V->slots[s].final_slot = VIRBUILD_BLOCK_NONE;
	for (uint32_t i = 0; i < V->block_order_count; i++)
		for (uint32_t q = V->blocks[V->block_order[i]].first_insn; q; q = V->insns[q].next)
			if (V->insns[q].insn.op == VIR_OP_LOAD && V->insns[q].slot_plus_one)
				V->slots[V->insns[q].slot_plus_one - 1].used = 1;
}

/* emission */
VPRIV bool virbuild_function_to_vir(ecx *e)
{
	virbuild_context *V = e->vw;
	cc *c = e->c;
	vir_function *f = &V->f;
	uint32_t lastloc = UINT32_MAX;

	vir_function_begin(f, X86(c) ? VIR_TARGET_X86_64_LINUX : DARWIN(c) ? VIR_TARGET_A64_DARWIN : VIR_TARGET_A64_LINUX, REF(RK_SYM, e->wk->fsym),
		  (T(c, c->syms[e->wk->fsym].ty)->flags & TF_VARIADIC) ? VIR_FUNCTION_FLAG_VARIADIC : 0);
	VIRBUILD_ARRAY_GROW(V->value_map, 0, V->value_map_capacity, V->insn_count + 1);
	memset(V->value_map, 0, (size_t)V->insn_count * sizeof *V->value_map);
	for (uint32_t i = 0; i < V->block_order_count; i++)
		V->blocks[V->block_order[i]].final_block = i;
	f->signature = vir_extra_add(f, &V->extra[V->function_signature], 3 + 2 * VIR_SIGNATURE_GROUP_COUNT(V->extra[V->function_signature]));
	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i];

		if (vir_block_add(f, 0) == VIR_BLOCK_NONE)
			return false;
		for (uint32_t p = V->blocks[b].first_parameter; p; p = V->insns[p].next)
			if (V->forward[p] == p && !(V->value_map[p] = vir_insn_add(f, VIR_OP_BPARAM, V->insns[p].insn.type, 0, 0, 0, 0)))
				return false;
		for (uint32_t q = V->blocks[b].first_insn; q; q = V->insns[q].next) {
			virbuild_preir_insn *x = &V->insns[q];

			if (virbuild_value_resolve(V, q) != q)
				continue;
			if (virbuild_promote_store_is_dead(V, q) || (x->insn.op == VIR_OP_MEMSET && (x->insn.flags & VIRBUILD_MEMSET_LIFETIME_FLAG)) ||
			    (x->insn.op == VIR_OP_STORE && x->slot_plus_one && !(x->insn.flags & VIR_INSN_FLAG_VOL) && !V->slots[x->slot_plus_one - 1].exposed &&
			     !V->slots[x->slot_plus_one - 1].used))
				continue;	/* (a dead store) */
			if (x->location != lastloc) {
				vir_location_add(f, x->location);
				lastloc = x->location;
			}
			if (!virbuild_to_vir_insn_emit(e, q, b))
				return false;
		}
		if (!V->blocks[b].terminator) {
			V->failure_reason = VIRBUILD_FAILURE(block_without_terminator);
			return false;
		}
		if (!virbuild_to_vir_insn_emit(e, V->blocks[b].terminator, b))
			return false;
	}
	if (vir_is_failed())
		return false;
	return true;
}

/* the passes of the table (pass-table.c) in order; with -fvir-passes given, through the instrumented
   step (timing, leaving out, checking); false: the function cannot go through VIR (V->why) */
VPRIV bool virbuild_function_emit(ecx *e)
{
	const virbuild_pass_switch *w = &e->E->vpass;
	uint32_t n, valid = 0;
	const virbuild_pass *t = virbuild_pass_list(&n);
	bool last = false;	/* what the last row without VIRBUILD_PASS_FLAG_IF_PREVIOUS_CHANGED returned */

	for (uint32_t i = 0; i < n; i++) {
		const virbuild_pass *p = &t[i];
		bool changed;

		if ((p->flags & VIRBUILD_PASS_FLAG_IF_PREVIOUS_CHANGED) && !last)
			continue;
		if (!w->active)
			changed = virbuild_pass_step(e, p);
		else if (w->skip >> i & 1)
			changed = false;
		else
			changed = virbuild_pass_step_instrumented(e, i, &valid);
		if (!(p->flags & VIRBUILD_PASS_FLAG_IF_PREVIOUS_CHANGED))
			last = changed;
	}
	return true;
}
