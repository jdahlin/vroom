/* Placeholder parameters: trivial ones removed, dead ones dropped. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* p's one value other than itself, 0 none (its operands are all p), VW_NOBLK two or more */
static uint32_t vw_trivial_value(vwst *V, uint32_t p)
{
	uint32_t same = 0, npred = V->b[V->p[p].blk].npred;

	for (uint32_t j = 0; j < npred; j++) {
		uint32_t o = vw_find(V, V->x[V->p[p].in.b + j]);

		if (o == p || o == same)
			continue;
		if (same)
			return VW_NOBLK;
		same = o;
	}
	return same;
}

/* a parameter all of whose operands are one value (or itself) is that value.  A worklist
 * (Braun et al.'s tryRemoveTrivialPhi): removing one parameter rechecks only the
 * parameters that read it, so a nest of N loops costs O(N), not one sweep per level */
VPRIV bool vw_trivial(ecx *e)
{
	vwst *V = e->vw;
	uint32_t np = V->np, nedge = 0, nw = 0;
	bool any = false;

	for (uint32_t i = 0; i < V->norder; i++) {
		vwb *k = &V->b[V->order[i]];

		for (uint32_t p = k->phead; p; p = V->p[p].next)
			if (V->fwd[p] == p)
				nedge += k->npred;
	}
	if (!nedge)
		return false;
	/* per value: its first user edge (0 none) and its last; per edge: the parameter reading
	 * the value, the next edge; the worklist and who is on it */
	uint32_t *head = calloc((size_t)np * 3 + (size_t)nedge * 2 + 2, sizeof *head);
	if (!head)
		vw_fail(e, VIRFAIL(out_of_memory));
	uint32_t *tail = head + np, *work = tail + np, *user = work + np, *next = user + nedge + 1;
	uint8_t *queued = calloc(np, 1);
	if (!queued) {
		free(head);
		vw_fail(e, VIRFAIL(out_of_memory));
	}
	uint32_t ne = 0;

	for (uint32_t i = 0; i < V->norder; i++) {
		vwb *k = &V->b[V->order[i]];

		for (uint32_t p = k->phead; p; p = V->p[p].next) {
			if (V->fwd[p] != p)
				continue;
			for (uint32_t j = 0; j < k->npred; j++) {
				uint32_t o = vw_find(V, V->x[V->p[p].in.b + j]);

				if (o == p || V->p[o].in.op != VO_BPARAM)
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
		if (V->fwd[p] != p || (same = vw_trivial_value(V, p)) == VW_NOBLK)
			continue;
		V->fwd[p] = same ? same : vw_undef(e, V->p[p].in.ty);
		any = true;
		/* its readers may be trivial now, and they read same from now on: the live ones
		 * other than same move to same's list (removed readers would make it grow along
		 * a nest of loops, each level rescanning the levels inside it) */
		bool keep = same && same < np && V->p[same].in.op == VO_BPARAM;
		uint32_t kh = 0, kt = 0;

		for (uint32_t d = head[p], dn; d; d = dn) {
			uint32_t u = user[d];

			dn = next[d];
			if (V->fwd[u] != u || u == same)
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
