/* Variables as SSA: Braun et al.'s construction on the pre-IR (placeholder parameters, reads and writes). */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "port/stack-guard.h"
#include "preir.h"

/* ---------------- variables (Braun) ---------------- */

static uint64_t vw_hkey(uint32_t b, uint32_t var) { return (uint64_t)b << 32 | var; }

/* every entry of the table empty */
VPRIV void vw_hgen(vwst *V)
{
	if (!++V->hgen) {	/* (the generations wrapped) */
		if (V->halloc)
			memset(V->h, 0, V->halloc * sizeof *V->h);
		V->hgen = 1;
	}
}

VPRIV uint32_t *vw_hslot(ecx *e, uint32_t b, uint32_t var, bool add)
{
	vwst *V = e->vw;
	uint64_t key = vw_hkey(b, var) + 1;

	if (add && (V->hn + 1) * 2 > V->hcap) {
		/* the next size, inside the allocation when a larger function left one: the table a
		 * function probes is as large as it needs, not as the largest one seen needed */
		uint32_t oc = V->hcap, nc = oc ? oc * 2 : VW_H0, n = 0;
		vwh *live = malloc(((size_t)V->hn + 1) * sizeof *live);

		if (!live)
			vw_fail(e, VIRFAIL(out_of_memory));
		for (uint32_t i = 0; i < oc; i++)
			if (V->h[i].gen == V->hgen)
				live[n++] = V->h[i];
		if (nc > V->halloc) {
			free(V->h);
			V->h = calloc(nc, sizeof *V->h);
			if (!V->h) {
				V->hcap = V->halloc = 0;
				free(live);
				vw_fail(e, VIRFAIL(out_of_memory));
			}
			V->halloc = nc;
			V->hgen = 1;
		} else {
			vw_hgen(V);
		}
		V->hcap = nc;
		for (uint32_t i = 0; i < n; i++) {
			uint32_t j = (uint32_t)(live[i].key * 0x9e3779b97f4a7c15ull >> 40) & (nc - 1);

			while (V->h[j].gen == V->hgen)
				j = (j + 1) & (nc - 1);
			V->h[j] = live[i];
			V->h[j].gen = V->hgen;
		}
		free(live);
	}
	if (!V->hcap)
		return NULL;
	for (uint32_t j = (uint32_t)(key * 0x9e3779b97f4a7c15ull >> 40) & (V->hcap - 1);; j = (j + 1) & (V->hcap - 1)) {
		if (V->h[j].gen != V->hgen) {
			if (!add)
				return NULL;
			V->h[j].key = key;
			V->h[j].gen = V->hgen;
			V->hn++;
			return &V->h[j].v;
		}
		if (V->h[j].key == key)
			return &V->h[j].v;
	}
}

VPRIV void vw_write(ecx *e, uint32_t var, uint32_t b, uint32_t v)
{
	*vw_hslot(e, b, var, true) = v;
	e->vw->b[b].def = 1;
}

/* the type a parameter of var gets when its value has type ty */
static uint32_t vw_phity(ecx *e, uint32_t var, uint32_t ty)
{
	if (var >= VW_VAR0)
		return e->vw->vty[var - VW_VAR0];
	if (ty == VT_I32)
		return VT_I64;
	return ty;
}

VPRIV uint32_t vw_newphi(ecx *e, uint32_t b, uint32_t var, uint32_t ty)
{
	vwst *V = e->vw;
	uint32_t i = vw_put(e, VO_BPARAM, ty, 0, var, VW_NOBLK, 0, b);
	vwb *k = &V->b[b];

	if (k->ptail)
		V->p[k->ptail].next = i;
	else
		k->phead = i;
	k->ptail = i;
	return i;
}

/* the operands of parameter p (block b) from b's predecessors, converted to its type */
VPRIV void vw_phiops(ecx *e, uint32_t p, uint32_t b)
{
	vwst *V = e->vw;
	uint32_t var = V->p[p].in.a, ty = V->p[p].in.ty, np = V->b[b].npred;
	uint32_t o = vw_xput(e, NULL, 0);

	GROW(V->x, V->nx, V->cx, np ? np : 1);
	V->nx += np;
	V->p[p].in.b = o;	/* (set first: a cycle back here reads p itself) */
	for (uint32_t k = 0; k < np; k++) {
		uint32_t q = V->pr[V->b[b].pred + k];
		uint32_t v = vw_readv(e, var, q, ty);

		V->x[o + k] = vw_conv_in(e, q, v, ty);
	}
}

VPRIV uint32_t vw_undef(ecx *e, uint32_t ty)
{
	if (ty == VT_F32 || ty == VT_F64)
		return vw_k(e, VO_FCONST, ty, 0, 0, 0);
	return vw_iconst(e, ty, 0);
}

/* var's value at the end of block b (during the walk: at the current point of the current block) */
VPRIV uint32_t vw_readv(ecx *e, uint32_t var, uint32_t b, uint32_t ty)
{
	vwst *V = e->vw;
	uint32_t *d = vw_hslot(e, b, var, false), v;

	if (d)
		return V->sealing ? vw_find(V, *d) : *d;
	if (!V->sealing) {	/* during the walk every block is open: a placeholder */
		v = vw_newphi(e, b, var, vw_phity(e, var, ty));
		vw_write(e, var, b, v);
		return v;
	}
	/* one level per block between a read and the definition it reaches (through vw_phiops
	 * at a join): a function of tens of thousands of blocks in a row goes the ordinary way */
	if (vstack_guard_low())
		vw_fail(e, VIRFAIL(variable_read_distance));
	if (!V->b[b].npred) {
		v = vw_undef(e, vw_phity(e, var, ty));
	} else if (V->b[b].npred == 1) {
		v = vw_readv(e, var, V->pr[V->b[b].pred], ty);
	} else {
		v = vw_newphi(e, b, var, vw_phity(e, var, ty));
		vw_write(e, var, b, v);
		vw_phiops(e, v, b);
		return vw_find(V, v);
	}
	vw_write(e, var, b, v);
	return v;
}
