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
	GROW(V->fwd, 0, V->cfwd, V->np + 1);
	for (uint32_t i = 0; i < V->np; i++)
		V->fwd[i] = i;
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			if (V->p[p].in.b == VIRBUILD_BLOCK_NONE)
				virbuild_parameter_operands_fill(e, p, b);
	}
}

/* stores to a slot that is not exposed and that nothing reads are dead */
VPRIV void virbuild_slot_uses_compute(ecx *e)
{
	virbuild_context *V = e->vw;

	for (uint32_t s = 0; s < V->nsl; s++)
		V->sl[s].used = 0, V->sl[s].fin = VIRBUILD_BLOCK_NONE;
	for (uint32_t i = 0; i < V->norder; i++)
		for (uint32_t q = V->b[V->order[i]].head; q; q = V->p[q].next)
			if (V->p[q].in.op == VIR_OP_LOAD && V->p[q].slot)
				V->sl[V->p[q].slot - 1].used = 1;
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
	GROW(V->map, 0, V->cmap, V->np + 1);
	memset(V->map, 0, (size_t)V->np * sizeof *V->map);
	for (uint32_t i = 0; i < V->norder; i++)
		V->b[V->order[i]].fin = i;
	f->signature = vir_extra_add(f, &V->x[V->fsig], 3 + 2 * VIR_SIGNATURE_GROUP_COUNT(V->x[V->fsig]));
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		if (vir_block_add(f, 0) == VIR_BLOCK_NONE)
			return false;
		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			if (V->fwd[p] == p && !(V->map[p] = vir_insn_add(f, VIR_OP_BPARAM, V->p[p].in.type, 0, 0, 0, 0)))
				return false;
		for (uint32_t q = V->b[b].head; q; q = V->p[q].next) {
			virbuild_preir_insn *x = &V->p[q];

			if (virbuild_value_resolve(V, q) != q)
				continue;
			if (virbuild_promote_store_is_dead(V, q) || (x->in.op == VIR_OP_MEMSET && (x->in.flags & VIRBUILD_MEMSET_LIFETIME_FLAG)) ||
			    (x->in.op == VIR_OP_STORE && x->slot && !(x->in.flags & VIR_INSN_FLAG_VOL) && !V->sl[x->slot - 1].exposed &&
			     !V->sl[x->slot - 1].used))
				continue;	/* (a dead store) */
			if (x->loc != lastloc) {
				vir_location_add(f, x->loc);
				lastloc = x->loc;
			}
			if (!virbuild_to_vir_insn_emit(e, q, b))
				return false;
		}
		if (!V->b[b].term) {
			V->why = VIRBUILD_FAILURE(block_without_terminator);
			return false;
		}
		if (!virbuild_to_vir_insn_emit(e, V->b[b].term, b))
			return false;
	}
	if (vir_is_failed())
		return false;
	return true;
}

/* the passes of the table (pass-table.c) in order; with VCC_VIR_PASSES set, through the instrumented
   step (timing, leaving out, checking); false: the function cannot go through VIR (V->why) */
VPRIV bool virbuild_function_emit(ecx *e)
{
	const virbuild_pass_switch *w = virbuild_pass_switch_get();
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
