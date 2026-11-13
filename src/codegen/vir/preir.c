/* The pre-IR: instructions and blocks, constants, terminators (PLAN-vir.md V2). */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

VPRIV virbuild_context *virbuild_context_get(ecx *e)
{
	if (!e->vw)
		e->vw = calloc(1, sizeof(virbuild_context));
	return e->vw;
}

VPRIV __attribute__((noreturn, cold)) void virbuild_fail(ecx *e, const char *why)
{
	if (!e->vfail)
		e->vfail = why;
	_longjmp(e->bail, 1);
}

VPRIV uint32_t virbuild_preir_insn_add(ecx *e, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c, uint32_t blk)
{
	virbuild_context *V = e->vw;
	uint32_t i = V->np;

	GROW(V->p, V->np, V->cp, 1);
	V->np++;
	V->p[i] = (virbuild_preir_insn){ { (uint8_t)op, (uint8_t)ty, (uint16_t)fl, a, b, c }, blk, 0, e->curloc, 0 };
	if (V->sealing) {
		GROW(V->fwd, i, V->cfwd, 1);
		V->fwd[i] = i;
	}
	return i;
}

/* an instruction at the end of block b's list */
VPRIV uint32_t virbuild_block_insn_append(ecx *e, uint32_t b, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t bb, uint32_t c)
{
	virbuild_context *V = e->vw;
	uint32_t i = virbuild_preir_insn_add(e, op, ty, fl, a, bb, c, b);
	virbuild_preir_block *k = &V->b[b];

	if (k->tail)
		V->p[k->tail].next = i;
	else
		k->head = i;
	k->tail = i;
	return i;
}

VPRIV uint32_t virbuild_insn_append(ecx *e, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c)
{
	return virbuild_block_insn_append(e, e->vw->cur, op, ty, fl, a, b, c);
}

VPRIV uint32_t virbuild_constant_make(ecx *e, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c)
{
	return virbuild_preir_insn_add(e, op, ty, 0, a, b, c, VIRBUILD_BLOCK_NONE);
}

VPRIV uint32_t virbuild_constant_integer(ecx *e, uint32_t ty, int64_t v)
{
	if (ty == VIR_TYPE_I32)
		return virbuild_constant_make(e, VIR_OP_ICONST, VIR_TYPE_I32, 0, (uint32_t)v, 0);
	return virbuild_constant_make(e, VIR_OP_ICONST, ty, 0, (uint32_t)v, (uint32_t)((uint64_t)v >> 32));
}

VPRIV uint32_t virbuild_extra_append(ecx *e, const uint32_t *w, uint32_t n)
{
	virbuild_context *V = e->vw;
	uint32_t o = V->nx;

	GROW(V->x, V->nx, V->cx, n ? n : 1);
	if (n)
		memcpy(&V->x[o], w, n * sizeof *w);
	V->nx += n;
	return o;
}

VPRIV uint32_t virbuild_block_new(ecx *e)
{
	virbuild_context *V = e->vw;
	uint32_t b = V->nb;

	GROW(V->b, V->nb, V->cb, 1);
	V->nb++;
	V->b[b] = (virbuild_preir_block){ 0 };
	V->b[b].fin = VIRBUILD_BLOCK_NONE;
	return b;
}

VPRIV uint32_t virbuild_label_block(ecx *e, uint32_t l)
{
	virbuild_context *V = e->vw;

	if (l >= V->clab) {
		uint32_t nc = V->clab ? V->clab : 256;

		while (nc <= l)
			nc *= 2;
		V->lab = realloc(V->lab, nc * sizeof *V->lab);
		if (!V->lab)
			virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
		memset(V->lab + V->clab, 0xff, (nc - V->clab) * sizeof *V->lab);
		V->clab = nc;
	}
	if (l >= V->labhi)
		V->labhi = l + 1;
	if (V->lab[l] == VIRBUILD_BLOCK_NONE)
		V->lab[l] = virbuild_block_new(e);
	return V->lab[l];
}

/* ends the current block with terminator t; code after it goes to a block nothing reaches */
VPRIV void virbuild_block_terminate(ecx *e, uint32_t op, uint32_t a, uint32_t b, uint32_t c, uint32_t fl)
{
	virbuild_context *V = e->vw;

	V->b[V->cur].term = virbuild_preir_insn_add(e, op, VIR_TYPE_VOID, fl, a, b, c, V->cur);
	V->cur = virbuild_block_new(e);
	V->nst = 0;
}

VPRIV void virbuild_branch_emit(ecx *e, uint32_t blk) { virbuild_block_terminate(e, VIR_OP_BR, blk, 0, 0, 0); }
