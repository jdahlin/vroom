/* The walker's lowering interface for calls, parameters and returns, and the start and end of a function. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "lowering-driver.h"

/* ---------------- calls ---------------- */

VPRIV void x64_abi_classify_named(codegen_context *e, ast_qualified_type t, walk_argument *a);

VPRIV void virbuild_classify(codegen_context *e, ast_qualified_type t, walk_argument *a, bool unnamed)
{
	cc *c = e->compile_context;
	uint64_t size = codegen_type_size(c, t);
	int k = KIND(c, t);

	if (CODEGEN_IS_X64(c) && !CODEGEN_IS_WIN64(c) && !e->msabi &&
	    (codegen_type_is_record(c, t) || k == AST_TYPE_INT128 || k == AST_TYPE_UINT128)) {
		x64_abi_classify_named(e, t, a);
		a->is_unnamed = unnamed;
		if (a->is_x87 || a->is_sse_up || a->is_wide_record || a->bool_lane_count ||
		    a->align > 16 || !size || size > 1024 || (!a->is_in_memory && !a->eightbyte_count))
			virbuild_fail(e, VIRBUILD_FAILURE(record_argument_or_result));
		return;
	}
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

static uint32_t virbuild_piece_class(const walk_argument *a, unsigned piece)
{
	if (a->classes[piece] == WALK_ARGUMENT_INTEGER) return VIR_CLASS_INT;
	return a->size == 4 ? VIR_CLASS_F32 : VIR_CLASS_F64;
}

static uint32_t virbuild_call_group_pack(const walk_argument *a, uint32_t *w, bool result)
{
	uint32_t pc = virbuild_piece_class(a, 0);

	if (a->is_in_memory) {
		w[0] = VIR_GROUP_WORD0(result ? VIR_GROUP_SRET : VIR_GROUP_MEM, 0, 0, 0, 0, 0, 0);
		w[1] = VIR_GROUP_WORD1(a->size, virbuild_integer_log2(a->align));
		return 0;
	}
	w[0] = VIR_GROUP_WORD0(VIR_GROUP_REG, a->eightbyte_count, pc, a->eightbyte_count == 2 ? virbuild_piece_class(a, 1) : 0, 0, 0, a->is_unnamed ? VIR_GROUP_UNNAMED : 0);
	w[1] = VIR_GROUP_WORD1(a->size, virbuild_integer_log2(a->align));	/* (the size: Apple packs stack arguments) */
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
		*rpc = virbuild_call_group_pack(&r, &V->extra[o + 1], true);
	}
	for (int k = 0; k < n; k++)
		virbuild_call_group_pack(&a[k], &V->extra[o + 3 + 2 * k], false);
	V->extra_count += 3 + 2 * (uint32_t)n;
	return o;
}

/* SysV wide arithmetic helpers use at most two arguments: each is either
 * one floating value or two integer words. The operands are already in SSA. */
VPRIV codegen_value virbuild_wide_helper_call(codegen_context *e, int helper, ast_qualified_type result_type,
	ast_qualified_type operand_type, unsigned nargs, const uint32_t *values)
{
	walk_argument args[2], result;
	virbuild_classify(e, operand_type, &args[0], false);
	args[1] = args[0];
	virbuild_classify(e, result_type, &result, false);
	uint32_t rpc, sig = virbuild_call_signature_make(e, args, nargs, false, result_type, &rpc);
	uint32_t count = nargs * args[0].eightbyte_count;
	uint32_t operands = virbuild_extra_append(e, &count, 1);
	virbuild_extra_append(e, values, count);
	uint32_t fn = virbuild_constant_make(e, VIR_OP_SYMADDR, VIR_TYPE_PTR, CODEGEN_FIXED_REFERENCE(helper), 0, 0);
	bool wide = result.eightbyte_count == 2;
	uint32_t call = virbuild_insn_append(e, VIR_OP_CALL, wide ? VIR_TYPE_MULTI : virbuild_pass_class_type(rpc), 0, fn, sig, operands);
	virbuild_store_table_clear(e, true);
	if (wide) {
		uint32_t lo = virbuild_insn_append(e, VIR_OP_PROJ, VIR_TYPE_I64, 0, call, 0, 0);
		uint32_t hi = virbuild_insn_append(e, VIR_OP_PROJ, VIR_TYPE_I64, 0, call, 1, 0);
		return virbuild_wide_pack(e, lo, hi);
	}
	int r = virbuild_ftreg(e);
	virbuild_register_write(e, r, call);
	return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r };
}

/* Pass a pointer to the walker's private, padded argument or result object. */
static uint32_t virbuild_call_slot_pointer(codegen_context *e, int64_t offset)
{
	codegen_lvalue at = { .kind = CODEGEN_LVALUE_FRAME, .offset = offset };
	int q = virbuild_laddr(e, &at);
	uint32_t ptr = virbuild_register_read(e, q, VIR_TYPE_PTR);
	virbuild_tfree(e, q);
	return ptr;
}

VPRIV codegen_value virbuild_call(codegen_context *e, uint32_t ref, codegen_value *fn, const walk_argument *a, int n, bool variadic, const walk_argument *r, ast_qualified_type rt)
{
	virbuild_context *V = e->vir_builder;
	cc *c = e->compile_context;
	uint32_t callee, sig, rpc, o, v;

	bool sret = r && r->is_in_memory;
	if (CODEGEN_IS_X64(c) && e->msabi != CODEGEN_IS_WIN64(c))	/* (the lowering knows the target's own convention only) */
		virbuild_fail(e, VIRBUILD_FAILURE(x64_calling_convention));
	if (fn) {
		callee = virbuild_walker_value_get(e, fn, VIR_TYPE_PTR);
		if (fn->kind == CODEGEN_VALUE_REGISTER)
			virbuild_tfree(e, fn->register_number);
	} else {
		callee = virbuild_constant_make(e, VIR_OP_SYMADDR, VIR_TYPE_PTR, ref, 0, 0);
	}
	sig = virbuild_call_signature_make(e, a, n, variadic, rt, &rpc);
	o = V->extra_count;
	uint32_t count = sret, word = 0;
	for (int k = 0; k < n; k++) count += a[k].is_in_memory ? 1 : a[k].eightbyte_count;
	VIRBUILD_ARRAY_GROW(V->extra, V->extra_count, V->extra_capacity, 1 + count);
	V->extra_count += 1 + count;
	V->extra[o] = count;
	if (sret) {
		uint32_t ptr = virbuild_call_slot_pointer(e, r->slot);
		V->extra[o + 1 + word++] = ptr;
	}
	for (int k = 0; k < n; k++) {
		for (int j = 0; j < (a[k].is_in_memory ? 1 : a[k].eightbyte_count); j++) {
			uint32_t ty = virbuild_pass_class_type(virbuild_piece_class(&a[k], j));
			uint32_t x = a[k].is_in_memory ? virbuild_call_slot_pointer(e, a[k].slot) :
				virbuild_call_word_load(e, a[k].slot + 8 * j, ty);
			V->extra[o + 1 + word++] = x;
		}
	}
	bool multi = r && r->eightbyte_count == 2;
	v = virbuild_insn_append(e, VIR_OP_CALL, multi ? VIR_TYPE_MULTI : rpc ? virbuild_pass_class_type(rpc) : VIR_TYPE_VOID, 0, callee, sig, o);
	virbuild_store_table_clear(e, true);
	if (r && !sret) {
		uint32_t values[2] = { v };
		for (int j = 0; multi && j < 2; j++)
			values[j] = virbuild_insn_append(e, VIR_OP_PROJ, virbuild_pass_class_type(virbuild_piece_class(r, j)), 0, v, j, 0);
		for (int j = 0; j < r->eightbyte_count; j++) {
			uint32_t pc = virbuild_piece_class(r, j);
			int q = pc == VIR_CLASS_INT ? virbuild_treg(e) : virbuild_ftreg(e);
			codegen_lvalue at = { .kind = CODEGEN_LVALUE_FRAME, .offset = r->slot + 8*j, .align = 8 };
			virbuild_register_write(e, q, values[j]);
			virbuild_store(e, q, &at, pc == VIR_CLASS_F32 ? 4 : 8);
			virbuild_tfree(e, q);
		}
		return codegen_value_constant(0);
	}
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

/* Memory parameters are copied into their ordinary local home at entry. */
VPRIV bool virbuild_byref(codegen_context *e, uint32_t s) { (void)e, (void)s; return false; }

VPRIV void virbuild_params(codegen_context *e, walk_argument *a, int n, const walk_argument *r, bool variadic)
{
	virbuild_context *V = e->vir_builder;
	cc *c = e->compile_context;
	const node *body = N(c->syms[e->walk_state->function_symbol].body);

	bool sret = r && r->is_in_memory;
	if (variadic && !CODEGEN_IS_DARWIN(c))
		virbuild_fail(e, VIRBUILD_FAILURE(variadic_function));
	if (CODEGEN_IS_X64(c) && e->msabi != CODEGEN_IS_WIN64(c))
		virbuild_fail(e, VIRBUILD_FAILURE(x64_calling_convention));
	uint32_t pv0 = V->insn_count;	/* the params first (SPEC 15.2: the allocator reads them from the ABI registers) */

	if (sret) {
		virbuild_insn_append(e, VIR_OP_PARAM, VIR_TYPE_PTR, 0, VIR_GROUP_RESULT, 0, 0);
		pv0++;
	}
	for (int k = 0; k < n; k++) {
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		ast_qualified_type ty = c->syms[sym].ty;
		for (int j = 0; j < (a[k].is_in_memory ? 1 : a[k].eightbyte_count); j++) {
			uint32_t vty = a[k].is_in_memory || codegen_type_is_pointer(c, ty) ? VIR_TYPE_PTR :
				virbuild_pass_class_type(virbuild_piece_class(&a[k], j));
			virbuild_insn_append(e, VIR_OP_PARAM, vty, 0, k, j, 0);
		}
	}
	uint32_t next = pv0;
	for (int k = 0; k < n; k++) {
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		ast_qualified_type ty = c->syms[sym].ty;
		uint32_t pc = a[k].classes[0] == WALK_ARGUMENT_SSE ? (a[k].size == 4 ? VIR_CLASS_F32 : VIR_CLASS_F64) : VIR_CLASS_INT;
		uint32_t v = next;
		next += a[k].is_in_memory ? 1 : a[k].eightbyte_count;
		codegen_lvalue home;
		int q;

		if (walk_type_is_in_memory(e, ty)) {
			codegen_lvalue at = { .kind = CODEGEN_LVALUE_FRAME, .offset = a[k].slot, .align = 8 };
			q = !a[k].is_in_memory && pc != VIR_CLASS_INT ? virbuild_ftreg(e) : virbuild_treg(e);
			virbuild_register_write(e, q, v);
			if (a[k].is_in_memory) {
				int dst = virbuild_laddr(e, &at);
				virbuild_copy(e, dst, q, a[k].size);
				virbuild_tfree(e, dst);
			} else {
				virbuild_store(e, q, &at, pc == VIR_CLASS_F32 ? 4 : 8);
				if (a[k].eightbyte_count == 2) {
					virbuild_tfree(e, q);
					q = a[k].classes[1] == WALK_ARGUMENT_INTEGER ? virbuild_treg(e) : virbuild_ftreg(e);
					at.offset += 8;
					virbuild_register_write(e, q, v + 1);
					virbuild_store(e, q, &at, 8);
				}
			}
			virbuild_tfree(e, q);
			continue;
		}
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
	if (sret) {
		int q = virbuild_treg(e);
		codegen_lvalue at = { .kind = CODEGEN_LVALUE_FRAME, .offset = r->slot, .align = 8 };
		virbuild_register_write(e, q, pv0 - 1);
		virbuild_store(e, q, &at, 8);
		virbuild_tfree(e, q);
	}
	V->parameter_count = next - pv0 + sret;
	{
		uint32_t rpc;

		V->function_signature = virbuild_call_signature_make(e, a, n, variadic, e->walk_state->return_type, &rpc);
	}
	if (CODEGEN_IS_WIN64(c) && c->syms[e->walk_state->function_symbol].sc != AST_STORAGE_STATIC && CODEGEN_IDENTIFIER_IS(c->syms[e->walk_state->function_symbol].name, "main")) {
		/* MinGW: main calls __main first (the constructors), as clang */
		e->has_calls = true;
		virbuild_call(e, CODEGEN_FIXED_REFERENCE(FN___MAIN), NULL, NULL, 0, false, NULL, codegen_type_primitive(AST_TYPE_VOID));
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
	V->indirect_dispatch = VIRBUILD_BLOCK_NONE;
	V->indirect_count = 0;
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
		uint32_t n = VIR_GROUP_PIECE_COUNT(w0), values[2];
		if (rty == VIR_TYPE_I64 && V->result_type == VIR_TYPE_PTR)
			rty = VIR_TYPE_PTR;
		for (uint32_t j = 0; j < n; j++) {
			uint32_t ty = j ? virbuild_pass_class_type(VIR_GROUP_CLASS(w0,j)) : rty;
			values[j] = virbuild_value_convert(e, virbuild_variable_read(e, VIRBUILD_RESULT_VARIABLE + j, V->current_block, ty), ty);
		}
		uint32_t o = virbuild_extra_append(e, &n, 1);
		virbuild_extra_append(e, values, n);
		V->blocks[V->current_block].terminator = virbuild_preir_insn_add(e, VIR_OP_RET, VIR_TYPE_VOID, 0, 0, 0, o, V->current_block);
	}
	/* Give all computed gotos one dispatch block. Incoming branches reconcile
	 * their SSA values before the indirect jump, so each public label has
	 * exactly one incoming allocator state from indirect control flow. */
	if (V->indirect_dispatch != VIRBUILD_BLOCK_NONE) {
		uint32_t targets = virbuild_extra_append(e, &V->indirect_count, 1);
		VIRBUILD_ARRAY_GROW(V->extra, V->extra_count, V->extra_capacity, V->indirect_count);
		for (uint32_t k = 0; k < V->indirect_count; k++)
			V->extra[V->extra_count++] = V->indirect_targets[k].block;
		uint32_t dispatch = V->indirect_dispatch;
		uint32_t addr = virbuild_variable_read(e, VIRBUILD_JUMP_VARIABLE, dispatch, VIR_TYPE_PTR);
		V->blocks[dispatch].terminator = virbuild_preir_insn_add(e, VIR_OP_IBR, VIR_TYPE_VOID, 0, addr, 0, targets, dispatch);
	} else if (V->indirect_count) {
		virbuild_fail(e, VIRBUILD_FAILURE(label_address));
	}
	if (!virbuild_function_emit(e))
		virbuild_fail(e, V->failure_reason ? V->failure_reason : VIRBUILD_FAILURE(build_failed));
}

VPRIV void virbuild_ecx_free(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;

	if (!V)
		return;
	free(V->indirect_targets);
	free(V->insns), free(V->blocks), free(V->extra), free(V->variable_entries), free(V->local_hash), free(V->forward), free(V->labels), free(V->slots), free(V->active_slots);
	free(V->stores), free(V->locations), free(V->variable_types), free(V->variable_is_signed), free(V->predecessors), free(V->value_map), free(V->block_order), free(V->block_map);
	free(V->lowering_slots), free(V->lowering_spills), free(V->lowering_labels), free(V->lowering_uses), free(V->lowering_edge_labels);
	free(V);
	e->vir_builder = NULL;
}
