/* The pre-IR: instructions and blocks, constants, terminators (PLAN-vir.md V2). */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

VPRIV virbuild_context *virbuild_context_get(codegen_context *e)
{
	if (!e->vw)
		e->vw = calloc(1, sizeof(virbuild_context));
	return e->vw;
}

VPRIV __attribute__((noreturn, cold)) void virbuild_fail(codegen_context *e, const char *why)
{
	if (!e->vfail)
		e->vfail = why;
	_longjmp(e->bail, 1);
}

VPRIV uint32_t virbuild_preir_insn_add(codegen_context *e, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c, uint32_t blk)
{
	virbuild_context *V = e->vw;
	uint32_t i = V->insn_count;

	VIRBUILD_ARRAY_GROW(V->insns, V->insn_count, V->insn_capacity, 1);
	V->insn_count++;
	V->insns[i] = (virbuild_preir_insn){ { (uint8_t)op, (uint8_t)ty, (uint16_t)fl, a, b, c }, blk, 0, e->curloc, 0 };
	if (V->sealing) {
		VIRBUILD_ARRAY_GROW(V->forward, i, V->forward_capacity, 1);
		V->forward[i] = i;
	}
	return i;
}

/* an instruction at the end of block b's list */
VPRIV uint32_t virbuild_block_insn_append(codegen_context *e, uint32_t b, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t bb, uint32_t c)
{
	virbuild_context *V = e->vw;
	uint32_t i = virbuild_preir_insn_add(e, op, ty, fl, a, bb, c, b);
	virbuild_preir_block *k = &V->blocks[b];

	if (k->last_insn)
		V->insns[k->last_insn].next = i;
	else
		k->first_insn = i;
	k->last_insn = i;
	return i;
}

VPRIV uint32_t virbuild_insn_append(codegen_context *e, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c)
{
	return virbuild_block_insn_append(e, e->vw->current_block, op, ty, fl, a, b, c);
}

VPRIV uint32_t virbuild_constant_make(codegen_context *e, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c)
{
	return virbuild_preir_insn_add(e, op, ty, 0, a, b, c, VIRBUILD_BLOCK_NONE);
}

VPRIV uint32_t virbuild_constant_integer(codegen_context *e, uint32_t ty, int64_t v)
{
	if (ty == VIR_TYPE_I32)
		return virbuild_constant_make(e, VIR_OP_ICONST, VIR_TYPE_I32, 0, (uint32_t)v, 0);
	return virbuild_constant_make(e, VIR_OP_ICONST, ty, 0, (uint32_t)v, (uint32_t)((uint64_t)v >> 32));
}

VPRIV uint32_t virbuild_extra_append(codegen_context *e, const uint32_t *w, uint32_t n)
{
	virbuild_context *V = e->vw;
	uint32_t o = V->extra_count;

	VIRBUILD_ARRAY_GROW(V->extra, V->extra_count, V->extra_capacity, n ? n : 1);
	if (n)
		memcpy(&V->extra[o], w, n * sizeof *w);
	V->extra_count += n;
	return o;
}

VPRIV uint32_t virbuild_block_new(codegen_context *e)
{
	virbuild_context *V = e->vw;
	uint32_t b = V->block_count;

	VIRBUILD_ARRAY_GROW(V->blocks, V->block_count, V->block_capacity, 1);
	V->block_count++;
	V->blocks[b] = (virbuild_preir_block){ 0 };
	V->blocks[b].final_block = VIRBUILD_BLOCK_NONE;
	return b;
}

VPRIV uint32_t virbuild_label_block(codegen_context *e, uint32_t l)
{
	virbuild_context *V = e->vw;

	if (l >= V->label_capacity) {
		uint32_t nc = V->label_capacity ? V->label_capacity : 256;

		while (nc <= l)
			nc *= 2;
		V->labels = realloc(V->labels, nc * sizeof *V->labels);
		if (!V->labels)
			virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
		memset(V->labels + V->label_capacity, 0xff, (nc - V->label_capacity) * sizeof *V->labels);
		V->label_capacity = nc;
	}
	if (l >= V->label_high)
		V->label_high = l + 1;
	if (V->labels[l] == VIRBUILD_BLOCK_NONE)
		V->labels[l] = virbuild_block_new(e);
	return V->labels[l];
}

/* ends the current block with terminator t; code after it goes to a block nothing reaches */
VPRIV void virbuild_block_terminate(codegen_context *e, uint32_t op, uint32_t a, uint32_t b, uint32_t c, uint32_t fl)
{
	virbuild_context *V = e->vw;

	V->blocks[V->current_block].terminator = virbuild_preir_insn_add(e, op, VIR_TYPE_VOID, fl, a, b, c, V->current_block);
	V->current_block = virbuild_block_new(e);
	V->store_count = 0;
}

VPRIV void virbuild_branch_emit(codegen_context *e, uint32_t blk) { virbuild_block_terminate(e, VIR_OP_BR, blk, 0, 0, 0); }
