/* Placeholder parameters: trivial ones removed, dead ones dropped. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* a parameter all of whose operands are one value (or itself) is that value */
VPRIV bool vw_trivial(ecx *e)
{
	vwst *V = e->vw;
	bool any = false;

	for (uint32_t i = 0; i < V->norder; i++) {
		vwb *k = &V->b[V->order[i]];

		for (uint32_t p = k->phead; p; p = V->p[p].next) {
			uint32_t same = 0;
			bool triv = true;

			if (V->fwd[p] != p)
				continue;
			for (uint32_t j = 0; j < k->npred; j++) {
				uint32_t o = vw_find(V, V->x[V->p[p].in.b + j]);

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
			V->fwd[p] = same ? same : vw_undef(e, V->p[p].in.ty);
			any = true;
		}
	}
	return any;
}

/* parameters nothing reads but other parameters (Braun's phi cycles, a local of a loop
 * that every path writes before reading) go: they would hold a register through the loop */
static void vw_mark(vwst *V, uint32_t *stk, uint32_t *n, uint32_t v)
{
	v = vw_find(V, v);
	if (V->p[v].in.op == VO_BPARAM && V->p[v].blk != VW_NOBLK && !V->map[v]) {
		V->map[v] = 1;
		stk[(*n)++] = v;
	}
}

VPRIV void vw_deadparams(ecx *e)
{
	vwst *V = e->vw;
	uint32_t *stk, n = 0;

	GROW(V->map, 0, V->cmap, V->np + 1);
	memset(V->map, 0, (size_t)V->np * sizeof *V->map);
	stk = malloc((size_t)(V->np + 1) * sizeof *stk);
	if (!stk)
		vw_fail(e, VIRFAIL(out_of_memory));
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t q = V->b[b].head;; q = V->p[q].next) {
			uint32_t x = q ? q : V->b[b].term;
			const vir_ins *in;
			const vir_opinfo *oi;

			if (!x)
				break;
			in = &V->p[x].in;
			oi = &vir_optab[in->op];
			if (vw_find(V, x) == x && in->op != VO_BR) {
				for (int k = 0; k < 3; k++)
					if (vw_isval(oi->k[k]) && !(in->op == VO_BRIF && k > 0))
						vw_mark(V, stk, &n, (&in->a)[k]);
				if (in->op == VO_CALL || in->op == VO_RET)
					for (uint32_t k = 0; k < V->x[in->c]; k++)
						vw_mark(V, stk, &n, V->x[in->c + 1 + k]);
			}
			if (!q)
				break;
		}
	}
	while (n) {	/* a live parameter's operands are live */
		uint32_t p = stk[--n], b = V->p[p].blk;

		for (uint32_t j = 0; j < V->b[b].npred; j++)
			vw_mark(V, stk, &n, V->x[V->p[p].in.b + j]);
	}
	for (uint32_t i = 0; i < V->norder; i++) {
		vwb *k = &V->b[V->order[i]];

		for (uint32_t p = k->phead; p; p = V->p[p].next)
			if (V->fwd[p] == p && !V->map[p])
				V->fwd[p] = vw_undef(e, V->p[p].in.ty);
	}
	free(stk);
}
