/* Variables as SSA: Braun et al.'s construction on the pre-IR (placeholder parameters, reads and writes). */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "port/stack-guard.h"
#include "preir.h"

/* ---------------- variables (Braun) ---------------- */

static uint64_t virbuild_variable_key(uint32_t b, uint32_t var) { return (uint64_t)b << 32 | var; }

/* every entry of the table empty */
VPRIV void virbuild_variable_table_clear(virbuild_context *V)
{
	if (!++V->variable_generation) {	/* (the generations wrapped) */
		if (V->variable_entry_allocated)
			memset(V->variable_entries, 0, V->variable_entry_allocated * sizeof *V->variable_entries);
		V->variable_generation = 1;
	}
}

VPRIV uint32_t *virbuild_variable_slot(codegen_context *e, uint32_t b, uint32_t var, bool add)
{
	virbuild_context *V = e->vir_builder;
	uint64_t key = virbuild_variable_key(b, var) + 1;

	if (add && (V->variable_entry_count + 1) * 2 > V->variable_entry_capacity) {
		/* the next size, inside the allocation when a larger function left one: the table a
		 * function probes is as large as it needs, not as the largest one seen needed */
		uint32_t oc = V->variable_entry_capacity, nc = oc ? oc * 2 : VIRBUILD_VARIABLE_TABLE_INITIAL, n = 0;
		virbuild_variable_entry *live = malloc(((size_t)V->variable_entry_count + 1) * sizeof *live);

		if (!live)
			virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
		for (uint32_t i = 0; i < oc; i++)
			if (V->variable_entries[i].generation == V->variable_generation)
				live[n++] = V->variable_entries[i];
		if (nc > V->variable_entry_allocated) {
			free(V->variable_entries);
			V->variable_entries = calloc(nc, sizeof *V->variable_entries);
			if (!V->variable_entries) {
				V->variable_entry_capacity = V->variable_entry_allocated = 0;
				free(live);
				virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
			}
			V->variable_entry_allocated = nc;
			V->variable_generation = 1;
		} else {
			virbuild_variable_table_clear(V);
		}
		V->variable_entry_capacity = nc;
		for (uint32_t i = 0; i < n; i++) {
			uint32_t j = (uint32_t)(live[i].key * 0x9e3779b97f4a7c15ull >> 40) & (nc - 1);

			while (V->variable_entries[j].generation == V->variable_generation)
				j = (j + 1) & (nc - 1);
			V->variable_entries[j] = live[i];
			V->variable_entries[j].generation = V->variable_generation;
		}
		free(live);
	}
	if (!V->variable_entry_capacity)
		return NULL;
	for (uint32_t j = (uint32_t)(key * 0x9e3779b97f4a7c15ull >> 40) & (V->variable_entry_capacity - 1);; j = (j + 1) & (V->variable_entry_capacity - 1)) {
		if (V->variable_entries[j].generation != V->variable_generation) {
			if (!add)
				return NULL;
			V->variable_entries[j].key = key;
			V->variable_entries[j].generation = V->variable_generation;
			V->variable_entry_count++;
			return &V->variable_entries[j].value;
		}
		if (V->variable_entries[j].key == key)
			return &V->variable_entries[j].value;
	}
}

VPRIV void virbuild_variable_write(codegen_context *e, uint32_t var, uint32_t b, uint32_t v)
{
	*virbuild_variable_slot(e, b, var, true) = v;
	e->vir_builder->blocks[b].has_definition = 1;
}

/* the type a parameter of var gets when its value has type ty */
static uint32_t virbuild_parameter_type(codegen_context *e, uint32_t var, uint32_t ty)
{
	if (var >= VIRBUILD_LOCAL_VARIABLE_FIRST)
		return e->vir_builder->variable_types[var - VIRBUILD_LOCAL_VARIABLE_FIRST];
	if (ty == VIR_TYPE_I32)
		return VIR_TYPE_I64;
	return ty;
}

VPRIV uint32_t virbuild_parameter_new(codegen_context *e, uint32_t b, uint32_t var, uint32_t ty)
{
	virbuild_context *V = e->vir_builder;
	uint32_t i = virbuild_preir_insn_add(e, VIR_OP_BPARAM, ty, 0, var, VIRBUILD_BLOCK_NONE, 0, b);
	virbuild_preir_block *k = &V->blocks[b];

	if (k->last_parameter)
		V->insns[k->last_parameter].next = i;
	else
		k->first_parameter = i;
	k->last_parameter = i;
	return i;
}

/* the operands of parameter p (block b) from b's predecessors, converted to its type */
VPRIV void virbuild_parameter_operands_fill(codegen_context *e, uint32_t p, uint32_t b)
{
	virbuild_context *V = e->vir_builder;
	uint32_t var = V->insns[p].insn.a, ty = V->insns[p].insn.type, np = V->blocks[b].predecessor_count;
	uint32_t o = virbuild_extra_append(e, NULL, 0);

	VIRBUILD_ARRAY_GROW(V->extra, V->extra_count, V->extra_capacity, np ? np : 1);
	V->extra_count += np;
	V->insns[p].insn.b = o;	/* (set first: a cycle back here reads p itself) */
	for (uint32_t k = 0; k < np; k++) {
		uint32_t q = V->predecessors[V->blocks[b].predecessor_first + k];
		uint32_t v = virbuild_variable_read(e, var, q, ty);

		V->extra[o + k] = virbuild_value_convert_in_block(e, q, v, ty);
	}
}

VPRIV uint32_t virbuild_undefined_value(codegen_context *e, uint32_t ty)
{
	if (ty == VIR_TYPE_F32 || ty == VIR_TYPE_F64)
		return virbuild_constant_make(e, VIR_OP_FCONST, ty, 0, 0, 0);
	return virbuild_constant_integer(e, ty, 0);
}

/* var's value at the end of block b (during the walk: at the current point of the current block) */
VPRIV uint32_t virbuild_variable_read(codegen_context *e, uint32_t var, uint32_t b, uint32_t ty)
{
	virbuild_context *V = e->vir_builder;
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
	if (!V->blocks[b].predecessor_count) {
		v = virbuild_undefined_value(e, virbuild_parameter_type(e, var, ty));
	} else if (V->blocks[b].predecessor_count == 1) {
		v = virbuild_variable_read(e, var, V->predecessors[V->blocks[b].predecessor_first], ty);
	} else {
		v = virbuild_parameter_new(e, b, var, virbuild_parameter_type(e, var, ty));
		virbuild_variable_write(e, var, b, v);
		virbuild_parameter_operands_fill(e, v, b);
		return virbuild_value_resolve(V, v);
	}
	virbuild_variable_write(e, var, b, v);
	return v;
}
