/* Variables as SSA: Braun et al.'s construction on the pre-IR (placeholder parameters, reads and writes). */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "port/stack-guard.h"
#include "preir.h"

/* ---------------- variables (Braun) ---------------- */

static uint64_t virbuild_variable_key(uint32_t b, uint32_t var) { return (uint64_t)b << 32 | var; }

/* every entry of the table empty */
VPRIV void virbuild_variable_table_clear(virbuild_context *V)
{
	if (!++V->hgen) {	/* (the generations wrapped) */
		if (V->halloc)
			memset(V->h, 0, V->halloc * sizeof *V->h);
		V->hgen = 1;
	}
}

VPRIV uint32_t *virbuild_variable_slot(ecx *e, uint32_t b, uint32_t var, bool add)
{
	virbuild_context *V = e->vw;
	uint64_t key = virbuild_variable_key(b, var) + 1;

	if (add && (V->hn + 1) * 2 > V->hcap) {
		/* the next size, inside the allocation when a larger function left one: the table a
		 * function probes is as large as it needs, not as the largest one seen needed */
		uint32_t oc = V->hcap, nc = oc ? oc * 2 : VIRBUILD_VARIABLE_TABLE_INITIAL, n = 0;
		virbuild_variable_entry *live = malloc(((size_t)V->hn + 1) * sizeof *live);

		if (!live)
			virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
		for (uint32_t i = 0; i < oc; i++)
			if (V->h[i].gen == V->hgen)
				live[n++] = V->h[i];
		if (nc > V->halloc) {
			free(V->h);
			V->h = calloc(nc, sizeof *V->h);
			if (!V->h) {
				V->hcap = V->halloc = 0;
				free(live);
				virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
			}
			V->halloc = nc;
			V->hgen = 1;
		} else {
			virbuild_variable_table_clear(V);
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

VPRIV void virbuild_variable_write(ecx *e, uint32_t var, uint32_t b, uint32_t v)
{
	*virbuild_variable_slot(e, b, var, true) = v;
	e->vw->b[b].def = 1;
}

/* the type a parameter of var gets when its value has type ty */
static uint32_t virbuild_parameter_type(ecx *e, uint32_t var, uint32_t ty)
{
	if (var >= VIRBUILD_LOCAL_VARIABLE_FIRST)
		return e->vw->vty[var - VIRBUILD_LOCAL_VARIABLE_FIRST];
	if (ty == VIR_TYPE_I32)
		return VIR_TYPE_I64;
	return ty;
}

VPRIV uint32_t virbuild_parameter_new(ecx *e, uint32_t b, uint32_t var, uint32_t ty)
{
	virbuild_context *V = e->vw;
	uint32_t i = virbuild_preir_insn_add(e, VIR_OP_BPARAM, ty, 0, var, VIRBUILD_BLOCK_NONE, 0, b);
	virbuild_preir_block *k = &V->b[b];

	if (k->ptail)
		V->p[k->ptail].next = i;
	else
		k->phead = i;
	k->ptail = i;
	return i;
}

/* the operands of parameter p (block b) from b's predecessors, converted to its type */
VPRIV void virbuild_parameter_operands_fill(ecx *e, uint32_t p, uint32_t b)
{
	virbuild_context *V = e->vw;
	uint32_t var = V->p[p].in.a, ty = V->p[p].in.type, np = V->b[b].npred;
	uint32_t o = virbuild_extra_append(e, NULL, 0);

	GROW(V->x, V->nx, V->cx, np ? np : 1);
	V->nx += np;
	V->p[p].in.b = o;	/* (set first: a cycle back here reads p itself) */
	for (uint32_t k = 0; k < np; k++) {
		uint32_t q = V->pr[V->b[b].pred + k];
		uint32_t v = virbuild_variable_read(e, var, q, ty);

		V->x[o + k] = virbuild_value_convert_in_block(e, q, v, ty);
	}
}

VPRIV uint32_t virbuild_undefined_value(ecx *e, uint32_t ty)
{
	if (ty == VIR_TYPE_F32 || ty == VIR_TYPE_F64)
		return virbuild_constant_make(e, VIR_OP_FCONST, ty, 0, 0, 0);
	return virbuild_constant_integer(e, ty, 0);
}

/* var's value at the end of block b (during the walk: at the current point of the current block) */
VPRIV uint32_t virbuild_variable_read(ecx *e, uint32_t var, uint32_t b, uint32_t ty)
{
	virbuild_context *V = e->vw;
	uint32_t *d = virbuild_variable_slot(e, b, var, false), v;

	if (d)
		return V->sealing ? virbuild_value_resolve(V, *d) : *d;
	if (!V->sealing) {	/* during the walk every block is open: a placeholder */
		v = virbuild_parameter_new(e, b, var, virbuild_parameter_type(e, var, ty));
		virbuild_variable_write(e, var, b, v);
		return v;
	}
	/* one level per block between a read and the definition it reaches (through virbuild_parameter_operands_fill
	 * at a join): a function of tens of thousands of blocks in a row goes the ordinary way */
	if (vstack_guard_low())
		virbuild_fail(e, VIRBUILD_FAILURE(variable_read_distance));
	if (!V->b[b].npred) {
		v = virbuild_undefined_value(e, virbuild_parameter_type(e, var, ty));
	} else if (V->b[b].npred == 1) {
		v = virbuild_variable_read(e, var, V->pr[V->b[b].pred], ty);
	} else {
		v = virbuild_parameter_new(e, b, var, virbuild_parameter_type(e, var, ty));
		virbuild_variable_write(e, var, b, v);
		virbuild_parameter_operands_fill(e, v, b);
		return virbuild_value_resolve(V, v);
	}
	virbuild_variable_write(e, var, b, v);
	return v;
}
