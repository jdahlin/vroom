/* The pre-IR: instructions and blocks, constants, terminators (PLAN-vir.md V2). */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

VPRIV vwst *vw_st(ecx *e)
{
	if (!e->vw)
		e->vw = calloc(1, sizeof(vwst));
	return e->vw;
}

VPRIV __attribute__((noreturn, cold)) void vw_fail(ecx *e, const char *why)
{
	if (!e->vfail)
		e->vfail = why;
	_longjmp(e->bail, 1);
}

VPRIV uint32_t vw_put(ecx *e, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c, uint32_t blk)
{
	vwst *V = e->vw;
	uint32_t i = V->np;

	GROW(V->p, V->np, V->cp, 1);
	V->np++;
	V->p[i] = (vwp){ { (uint8_t)op, (uint8_t)ty, (uint16_t)fl, a, b, c }, blk, 0, e->curloc, 0 };
	if (V->sealing) {
		GROW(V->fwd, i, V->cfwd, 1);
		V->fwd[i] = i;
	}
	return i;
}

/* an instruction at the end of block b's list */
VPRIV uint32_t vw_ins_in(ecx *e, uint32_t b, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t bb, uint32_t c)
{
	vwst *V = e->vw;
	uint32_t i = vw_put(e, op, ty, fl, a, bb, c, b);
	vwb *k = &V->b[b];

	if (k->tail)
		V->p[k->tail].next = i;
	else
		k->head = i;
	k->tail = i;
	return i;
}

VPRIV uint32_t vw_ins(ecx *e, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c)
{
	return vw_ins_in(e, e->vw->cur, op, ty, fl, a, b, c);
}

VPRIV uint32_t vw_k(ecx *e, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c)
{
	return vw_put(e, op, ty, 0, a, b, c, VW_NOBLK);
}

VPRIV uint32_t vw_iconst(ecx *e, uint32_t ty, int64_t v)
{
	if (ty == VT_I32)
		return vw_k(e, VO_ICONST, VT_I32, 0, (uint32_t)v, 0);
	return vw_k(e, VO_ICONST, ty, 0, (uint32_t)v, (uint32_t)((uint64_t)v >> 32));
}

VPRIV uint32_t vw_xput(ecx *e, const uint32_t *w, uint32_t n)
{
	vwst *V = e->vw;
	uint32_t o = V->nx;

	GROW(V->x, V->nx, V->cx, n ? n : 1);
	if (n)
		memcpy(&V->x[o], w, n * sizeof *w);
	V->nx += n;
	return o;
}

VPRIV uint32_t vw_newblk(ecx *e)
{
	vwst *V = e->vw;
	uint32_t b = V->nb;

	GROW(V->b, V->nb, V->cb, 1);
	V->nb++;
	V->b[b] = (vwb){ 0 };
	V->b[b].fin = VW_NOBLK;
	return b;
}

VPRIV uint32_t vw_lblk(ecx *e, uint32_t l)
{
	vwst *V = e->vw;

	if (l >= V->clab) {
		uint32_t nc = V->clab ? V->clab : 256;

		while (nc <= l)
			nc *= 2;
		V->lab = realloc(V->lab, nc * sizeof *V->lab);
		if (!V->lab)
			vw_fail(e, VIRFAIL(out_of_memory));
		memset(V->lab + V->clab, 0xff, (nc - V->clab) * sizeof *V->lab);
		V->clab = nc;
	}
	if (l >= V->labhi)
		V->labhi = l + 1;
	if (V->lab[l] == VW_NOBLK)
		V->lab[l] = vw_newblk(e);
	return V->lab[l];
}

/* ends the current block with terminator t; code after it goes to a block nothing reaches */
VPRIV void vw_term(ecx *e, uint32_t op, uint32_t a, uint32_t b, uint32_t c, uint32_t fl)
{
	vwst *V = e->vw;

	V->b[V->cur].term = vw_put(e, op, VT_VOID, fl, a, b, c, V->cur);
	V->cur = vw_newblk(e);
	V->nst = 0;
}

VPRIV void vw_br(ecx *e, uint32_t blk) { vw_term(e, VO_BR, blk, 0, 0, 0); }
