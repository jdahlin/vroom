/* The walker's lowering interface for calls, parameters and returns, and the start and end of a function. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* ---------------- calls ---------------- */

/* the classification for arm64 (the signature's groups): scalars only for now */
VPRIV void virbuild_classify(codegen_context *e, ast_qualified_type t, walk_argument *a, bool unnamed)
{
	cc *c = e->compile_context;
	uint64_t size = codegen_type_size(c, t);
	int k = KIND(c, t);

	memset(a, 0, sizeof *a);
	a->size = size;
	a->align = codegen_type_alignment(c, t) > 8 ? codegen_type_alignment(c, t) : 8;
	a->is_unnamed = unnamed;
	if (codegen_type_is_record(c, t) || codegen_type_is_array(c, t) || codegen_type_is_vector(c, t) || codegen_type_is_wide(c, t) || k == AST_TYPE_LONG_DOUBLE || k == AST_TYPE_FLOAT128 ||
	    k == AST_TYPE_FLOAT16 || k == AST_TYPE_BFLOAT16 || size > 8)
		virbuild_fail(e, VIRBUILD_FAILURE(record_argument_or_result));
	a->eightbyte_count = 1;
	a->classes[0] = a->classes[1] = codegen_type_is_float(c, t) ? WALK_ARGUMENT_SSE : WALK_ARGUMENT_INTEGER;
}

static uint32_t virbuild_call_group_pack(const walk_argument *a, uint32_t *w)
{
	uint32_t pc = a->classes[0] == WALK_ARGUMENT_SSE ? (a->size == 4 ? VIR_CLASS_F32 : VIR_CLASS_F64) : VIR_CLASS_INT;

	w[0] = VIR_GROUP_WORD0(VIR_GROUP_REG, 1, pc, 0, 0, 0, a->is_unnamed ? VIR_GROUP_UNNAMED : 0);
	w[1] = VIR_GROUP_WORD1(a->size, 0);	/* (the size: Apple packs stack arguments) */
	return pc;
}

static uint32_t virbuild_pass_class_type(uint32_t pc) { return pc == VIR_CLASS_F32 ? VIR_TYPE_F32 : pc == VIR_CLASS_F64 ? VIR_TYPE_F64 : VIR_TYPE_I64; }

/* a slot word's value: the store table's, else a load */
static uint32_t virbuild_call_word_load(codegen_context *e, int64_t off, uint32_t ty)
{
	virbuild_context *V = e->vir_builder;
	int64_t rel;
	uint32_t slot = virbuild_slot_find(e, off, 8, &rel);

	for (uint32_t i = V->store_count; i-- > 0;)
		if (V->stores[i].slot == slot && V->stores[i].offset == off) {
			uint32_t x = V->stores[i].v;

			if (V->stores[i].size == 8 || (V->stores[i].size == 4 && ty == VIR_TYPE_F32))
				return virbuild_value_convert(e, x, ty == VIR_TYPE_I64 && virbuild_value_type(e, x) == VIR_TYPE_PTR ? VIR_TYPE_PTR : ty);
		}
	V->slots[slot].loads++;
	{
		uint32_t v = virbuild_insn_append(e, VIR_OP_LOAD, ty, virbuild_memory_flags_make(ty == VIR_TYPE_F32 ? 4 : 8, 8, false), virbuild_slot_address(e, slot, rel), 0, 0);

		V->insns[v].slot_plus_one = slot + 1;
		return v;
	}
}

static uint32_t virbuild_call_signature_make(codegen_context *e, const walk_argument *a, int n, bool variadic, ast_qualified_type rt, uint32_t *rpc)
{
	virbuild_context *V = e->vir_builder;
	cc *c = e->compile_context;
	uint32_t o = V->extra_count;

	VIRBUILD_ARRAY_GROW(V->extra, V->extra_count, V->extra_capacity, 3 + 2 * (uint32_t)n);
	V->extra[o] = VIR_SIGNATURE_HEADER((uint32_t)n, variadic);
	if (KIND(c, rt) == AST_TYPE_VOID) {
		V->extra[o + 1] = VIR_GROUP_WORD0(VIR_GROUP_NONE, 0, 0, 0, 0, 0, 0), V->extra[o + 2] = 0;
		*rpc = 0;
	} else {
		walk_argument r;

		virbuild_classify(e, rt, &r, false);
		*rpc = virbuild_call_group_pack(&r, &V->extra[o + 1]);
	}
	for (int k = 0; k < n; k++)
		virbuild_call_group_pack(&a[k], &V->extra[o + 3 + 2 * k]);
	V->extra_count += 3 + 2 * (uint32_t)n;
	return o;
}

VPRIV codegen_value virbuild_call(codegen_context *e, uint32_t ref, codegen_value *fn, const walk_argument *a, int n, bool variadic, const walk_argument *r, ast_qualified_type rt)
{
	virbuild_context *V = e->vir_builder;
	cc *c = e->compile_context;
	uint32_t callee, sig, rpc, o, v;
	uint32_t *w;

	if (r)
		virbuild_fail(e, VIRBUILD_FAILURE(record_result));
	for (int k = 0; k < n; k++)
		if (a[k].is_in_memory || a[k].eightbyte_count != 1)
			virbuild_fail(e, VIRBUILD_FAILURE(record_argument));
	if (fn) {
		callee = virbuild_walker_value_get(e, fn, VIR_TYPE_PTR);
		if (fn->kind == CODEGEN_VALUE_REGISTER)
			virbuild_tfree(e, fn->register_number);
	} else {
		callee = virbuild_constant_make(e, VIR_OP_SYMADDR, VIR_TYPE_PTR, ref, 0, 0);
	}
	sig = virbuild_call_signature_make(e, a, n, variadic, rt, &rpc);
	o = V->extra_count;
	VIRBUILD_ARRAY_GROW(V->extra, V->extra_count, V->extra_capacity, 1 + (uint32_t)n);
	V->extra_count += 1 + (uint32_t)n;
	V->extra[o] = (uint32_t)n;
	for (int k = 0; k < n; k++) {
		uint32_t pc = a[k].classes[0] == WALK_ARGUMENT_SSE ? (a[k].size == 4 ? VIR_CLASS_F32 : VIR_CLASS_F64) : VIR_CLASS_INT;
		uint32_t x = virbuild_call_word_load(e, a[k].slot, virbuild_pass_class_type(pc));

		w = &V->extra[o + 1 + k];
		*w = x;
	}
	v = virbuild_insn_append(e, VIR_OP_CALL, rpc ? virbuild_pass_class_type(rpc) : VIR_TYPE_VOID, 0, callee, sig, o);
	virbuild_store_table_clear(e, true);
	if (!rpc)
		return codegen_value_constant(0);
	if (rpc != VIR_CLASS_INT) {
		int q = virbuild_ftreg(e);

		virbuild_register_write(e, q, v);
		return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)q };
	}
	{
		int q = virbuild_treg(e), size = walk_type_size(c, rt);
		bool sgn = walk_type_is_signed(c, rt);

		if (codegen_type_is_pointer(c, rt))
			v = virbuild_value_convert(e, v, VIR_TYPE_PTR);
		virbuild_register_write(e, q, v);
		if (size == 4 && !sgn)
			virbuild_register_write(e, q, virbuild_value_convert(e, v, VIR_TYPE_I32));
		else if (size < 8)
			virbuild_ext(e, q, size, sgn);
		return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)q };
	}
}

VPRIV void virbuild_params(codegen_context *e, walk_argument *a, int n, const walk_argument *r, bool variadic)
{
	virbuild_context *V = e->vir_builder;
	cc *c = e->compile_context;
	const node *body = N(c->syms[e->walk_state->function_symbol].body);

	if (r)
		virbuild_fail(e, VIRBUILD_FAILURE(record_result));
	if (variadic && !CODEGEN_IS_DARWIN(c))
		virbuild_fail(e, VIRBUILD_FAILURE(variadic_function));
	uint32_t pv0 = V->insn_count;	/* the params first (SPEC 15.2: the allocator reads them from the ABI registers) */

	for (int k = 0; k < n; k++) {
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		ast_qualified_type ty = c->syms[sym].ty;
		uint32_t pc = a[k].classes[0] == WALK_ARGUMENT_SSE ? (a[k].size == 4 ? VIR_CLASS_F32 : VIR_CLASS_F64) : VIR_CLASS_INT;

		if (a[k].is_in_memory || a[k].eightbyte_count != 1)
			virbuild_fail(e, VIRBUILD_FAILURE(record_parameter));
		if (virbuild_insn_append(e, VIR_OP_PARAM, pc == VIR_CLASS_INT && codegen_type_is_pointer(c, ty) ? VIR_TYPE_PTR : virbuild_pass_class_type(pc), 0, (uint32_t)k, 0, 0) != pv0 + (uint32_t)k)
			virbuild_fail(e, VIRBUILD_FAILURE(parameters));
	}
	for (int k = 0; k < n; k++) {
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		ast_qualified_type ty = c->syms[sym].ty;
		uint32_t pc = a[k].classes[0] == WALK_ARGUMENT_SSE ? (a[k].size == 4 ? VIR_CLASS_F32 : VIR_CLASS_F64) : VIR_CLASS_INT;
		uint32_t v = pv0 + (uint32_t)k;
		codegen_lvalue home;
		int q;

		q = pc == VIR_CLASS_INT ? virbuild_treg(e) : virbuild_ftreg(e);
		virbuild_register_write(e, q, v);
		if (pc == VIR_CLASS_INT && walk_type_size(c, ty) == 4)
			virbuild_register_write(e, q, virbuild_value_convert(e, v, VIR_TYPE_I32));	/* (the low half is the int; an i32 for the walker) */
		else if (pc == VIR_CLASS_INT && walk_type_size(c, ty) < 4)
			virbuild_ext(e, q, walk_type_size(c, ty), walk_type_is_signed(c, ty));
		if (virbuild_local(e, sym, &home)) {
			virbuild_store(e, q, &home, pc == VIR_CLASS_INT ? walk_type_size(c, ty) : (int)a[k].size);
		} else {
			codegen_lvalue s = { .kind = CODEGEN_LVALUE_FRAME, .offset = a[k].slot, .align = 8 };

			virbuild_store(e, q, &s, 8);
		}
		virbuild_tfree(e, q);
	}
	V->parameter_count = (uint32_t)n;
	{
		uint32_t rpc;

		V->function_signature = virbuild_call_signature_make(e, a, n, variadic, e->walk_state->return_type, &rpc);
	}
}

VPRIV void virbuild_ret(codegen_context *e, codegen_value *v, ast_qualified_type t, uint32_t lbl)
{
	virbuild_context *V = e->vir_builder;
	cc *c = e->compile_context;

	if (v) {
		uint32_t x;

		if (codegen_type_is_float(c, t)) {
			x = virbuild_walker_value_get(e, v, virbuild_float_type(e, t));
		} else {
			int size = walk_type_size(c, t);
			bool sgn = walk_type_is_signed(c, t);

			if (v->kind == CODEGEN_VALUE_CONSTANT) {
				int64_t k = (int64_t)v->integer;

				if (size < 8)
					k = sgn ? (int64_t)((uint64_t)k << (64 - 8 * size)) >> (64 - 8 * size) :
						  (int64_t)((uint64_t)k & ((1ull << (8 * size)) - 1));
				x = virbuild_constant_integer(e, codegen_type_is_pointer(c, t) ? VIR_TYPE_PTR : VIR_TYPE_I64, k);
			} else {
				int q = virbuild_toreg(e, v, size);

				if (size < 4)	/* (an int's upper half is unspecified in AAPCS64: a free zero extension) */
					virbuild_ext(e, q, size, sgn);
				else if (size == 4)
					virbuild_register_write(e, q, virbuild_register_read(e, q, VIR_TYPE_I32));
				x = virbuild_register_read(e, q, codegen_type_is_pointer(c, t) ? VIR_TYPE_PTR : VIR_TYPE_I64);
				virbuild_tfree(e, q);
			}
		}
		virbuild_variable_write(e, VIRBUILD_RESULT_VARIABLE, V->current_block, x);
		V->result_type = virbuild_value_type(e, x);
	}
	virbuild_branch_emit(e, virbuild_label_block(e, lbl));
}

/* ---------------- the function ---------------- */

VPRIV void virbuild_begin(codegen_context *e, uint32_t s)
{
	virbuild_context *V = virbuild_context_get(e);

	(void)s;
	V->insn_count = 1;	/* (0 is no instruction) */
	VIRBUILD_ARRAY_GROW(V->insns, 0, V->insn_capacity, 1);
	V->insns[0] = (virbuild_preir_insn){ 0 };
	V->block_count = V->extra_count = V->slot_count = V->active_slot_count = V->store_count = V->location_count = V->promoted_count = 0;
	V->live_names[0] = V->live_names[1] = V->live_names[2] = 0;
	V->live_float_names = 0;
	memset(V->float_products, 0, sizeof V->float_products);
	V->variable_entry_count = 0;
	virbuild_variable_table_clear(V);
	V->variable_entry_capacity = V->variable_entry_allocated < VIRBUILD_VARIABLE_TABLE_INITIAL ? V->variable_entry_allocated : VIRBUILD_VARIABLE_TABLE_INITIAL;
	if (V->local_hash_capacity)
		memset(V->local_hash, 0, V->local_hash_capacity * sizeof *V->local_hash);
	V->local_hash_capacity = V->local_hash_allocated < 256 ? V->local_hash_allocated : 256;
	if (V->label_high)	/* (only the labels the last function used) */
		memset(V->labels, 0xff, V->label_high * sizeof *V->labels);
	V->label_high = 0;
	V->sealing = false;
	V->failure_reason = NULL;	/* (one function's reason is not the next one's) */
	V->result_type = VIR_TYPE_VOID;
	V->parameter_count = 0;
	V->current_block = virbuild_block_new(e);
	e->debug_row_count = 0;
}

VPRIV void virbuild_finish(codegen_context *e, uint32_t s)
{
	virbuild_context *V = e->vir_builder;

	(void)s;
	{	/* the return block: ret, with the result if there is one */
		uint32_t w0 = V->extra[V->function_signature + 1], rty = VIR_GROUP_KIND(w0) == VIR_GROUP_REG ? virbuild_pass_class_type(VIR_GROUP_CLASS(w0, 0)) : VIR_TYPE_VOID;
		uint32_t o, rv;

		if (rty == VIR_TYPE_I64 && V->result_type == VIR_TYPE_PTR)
			rty = VIR_TYPE_PTR;
		rv = rty != VIR_TYPE_VOID ? virbuild_value_convert(e, virbuild_variable_read(e, VIRBUILD_RESULT_VARIABLE, V->current_block, rty), rty) : 0;
		o = V->extra_count;

		VIRBUILD_ARRAY_GROW(V->extra, V->extra_count, V->extra_capacity, 2);
		V->extra[V->extra_count++] = rv ? 1 : 0;
		if (rv)
			V->extra[V->extra_count++] = rv;
		V->blocks[V->current_block].terminator = virbuild_preir_insn_add(e, VIR_OP_RET, VIR_TYPE_VOID, 0, 0, 0, o, V->current_block);
	}
	if (!virbuild_function_emit(e))
		virbuild_fail(e, V->failure_reason ? V->failure_reason : VIRBUILD_FAILURE(build_failed));
}

VPRIV void virbuild_ecx_free(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;

	if (!V)
		return;
	free(V->insns), free(V->blocks), free(V->extra), free(V->variable_entries), free(V->local_hash), free(V->forward), free(V->labels), free(V->slots), free(V->active_slots);
	free(V->stores), free(V->locations), free(V->variable_types), free(V->variable_is_signed), free(V->predecessors), free(V->value_map), free(V->block_order), free(V->block_map);
	free(V->lowering_slots), free(V->lowering_spills), free(V->lowering_labels), free(V->lowering_uses), free(V->lowering_edge_labels);
	free(V);
	e->vir_builder = NULL;
}
