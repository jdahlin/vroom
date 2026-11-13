/* The builder's passes in order, and the emission of the function. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"
#include "vectorize.h"

VPRIV bool vw_emit(ecx *e)
{
	vwst *V = e->vw;
	cc *c = e->c;
	vir_fn *f = &V->f;
	uint32_t lastloc = UINT32_MAX;

	vw_thread(e);
	vw_rpo(e);
	vw_preds(e);
	/* seal: every placeholder gets its operands, then trivial ones go */
	V->sealing = true;
	GROW(V->fwd, 0, V->cfwd, V->np + 1);
	for (uint32_t i = 0; i < V->np; i++)
		V->fwd[i] = i;
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			if (V->p[p].in.b == VW_NOBLK)
				vw_phiops(e, p, b);
	}
	vw_promote(e);
	while (vw_trivial(e))
		;
	vw_deadparams(e);
	vw_simplify(e);
	/* simplifying can free a slot's address (an inlined call's pointer argument parked as an
	   integer comes back as i2p(p2i $slot)): promote again, then simplify what it forwards */
	if (vw_promote(e)) {
		while (vw_trivial(e))
			;
		vw_deadparams(e);
		vw_simplify(e);
	}
	if (vw_condmerge(e))
		while (vw_trivial(e))
			;
	if (vw_ifconv(e))
		while (vw_trivial(e))
			;
	vw_vectorize(e);
	vw_split(e);
	/* stores to a slot that is not exposed and that nothing reads are dead */
	for (uint32_t s = 0; s < V->nsl; s++)
		V->sl[s].used = 0, V->sl[s].fin = VW_NOBLK;
	for (uint32_t i = 0; i < V->norder; i++)
		for (uint32_t q = V->b[V->order[i]].head; q; q = V->p[q].next)
			if (V->p[q].in.op == VO_LOAD && V->p[q].slot)
				V->sl[V->p[q].slot - 1].used = 1;
	/* emission */
	vir_begin(f, X86(c) ? VTG_X86_64_LINUX : DARWIN(c) ? VTG_A64_DARWIN : VTG_A64_LINUX, REF(RK_SYM, e->wk->fsym),
		  (T(c, c->syms[e->wk->fsym].ty)->flags & TF_VARIADIC) ? VF_VARIADIC : 0);
	GROW(V->map, 0, V->cmap, V->np + 1);
	memset(V->map, 0, (size_t)V->np * sizeof *V->map);
	for (uint32_t i = 0; i < V->norder; i++)
		V->b[V->order[i]].fin = i;
	f->sig = vir_extra(f, &V->x[V->fsig], 3 + 2 * VSIG_NGROUPS(V->x[V->fsig]));
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		if (vir_blk_add(f, 0) == VIR_NOBLK)
			return false;
		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			if (V->fwd[p] == p && !(V->map[p] = vir_ins_add(f, VO_BPARAM, V->p[p].in.ty, 0, 0, 0, 0)))
				return false;
		for (uint32_t q = V->b[b].head; q; q = V->p[q].next) {
			vwp *x = &V->p[q];

			if (vw_find(V, q) != q)
				continue;
			if (vw_promdead(V, q) || (x->in.op == VO_MEMSET && (x->in.fl & VW_LIFETIME)) ||
			    (x->in.op == VO_STORE && x->slot && !(x->in.fl & VIF_VOL) && !V->sl[x->slot - 1].exposed &&
			     !V->sl[x->slot - 1].used))
				continue;	/* (a dead store) */
			if (x->loc != lastloc) {
				vir_loc_add(f, x->loc);
				lastloc = x->loc;
			}
			if (!vw_emit_ins(e, q, b))
				return false;
		}
		if (!V->b[b].term) {
			V->why = VIRFAIL(block_without_terminator);
			return false;
		}
		if (!vw_emit_ins(e, V->b[b].term, b))
			return false;
	}
	if (vir_failed())
		return false;
	return vir_canon(f);
}
