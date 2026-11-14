/* The walker's lowering interface for builtins and control: atomics, varargs, labels, jumps; what the builder does not handle. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* ---------------- builtins and the rest ---------------- */

VPRIV codegen_value virbuild_bitop(codegen_context *e, int bi, int size, codegen_value a)
{
	uint32_t ty = virbuild_integer_type(size), x = virbuild_walker_value_get(e, &a, ty), op;
	int r = virbuild_result_register_pick(e, &a, NULL);

	switch (bi) {
	case CC_BUILTIN_CLZ: case CC_BUILTIN_CLZL: case CC_BUILTIN_CLZLL: op = VIR_OP_CLZ; break;
	case CC_BUILTIN_CTZ: case CC_BUILTIN_CTZL: case CC_BUILTIN_CTZLL: op = VIR_OP_CTZ; break;
	case CC_BUILTIN_POPCOUNT: case CC_BUILTIN_POPCOUNTL: case CC_BUILTIN_POPCOUNTLL: op = VIR_OP_POPCNT; break;
	case CC_BUILTIN_BSWAP32: case CC_BUILTIN_BSWAP64: op = VIR_OP_BSWAP; break;
	case CC_BUILTIN_BSWAP16:
		x = virbuild_insn_append(e, VIR_OP_BSWAP, VIR_TYPE_I32, 0, virbuild_value_convert(e, x, VIR_TYPE_I32), 0, 0);
		virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_LSHR, VIR_TYPE_I32, 0, x, virbuild_constant_integer(e, VIR_TYPE_I32, 16), 0));
		return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r };
	case CC_BUILTIN_PARITY: case CC_BUILTIN_PARITYL: case CC_BUILTIN_PARITYLL:
		x = virbuild_insn_append(e, VIR_OP_POPCNT, ty, 0, x, 0, 0);
		virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_AND, ty, 0, x, virbuild_constant_integer(e, ty, 1), 0));
		return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r };
	default: virbuild_fail(e, VIRBUILD_FAILURE(unsupported_builtin));
	}
	virbuild_register_write(e, r, virbuild_insn_append(e, op, ty, op == VIR_OP_CTZ || op == VIR_OP_CLZ ? VIR_INSN_FLAG_NZ : 0, x, 0, 0));
	return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r };
}

/* sequentially consistent; a result is zero-extended (an i32 for 4 bytes or less) */
VPRIV int virbuild_atomic(codegen_context *e, int op, int size, int rp, int rv, int rexp)
{
	uint32_t ty = virbuild_integer_type(size), fl = virbuild_memory_flags_make(size, (uint32_t)size, false), p, v = 0;
	int r;

	if (op == WALK_ATOMIC_FENCE) {
		virbuild_insn_append(e, VIR_OP_AFENCE, VIR_TYPE_VOID, 0, 0, 0, 0);
		virbuild_store_table_clear(e, true);
		return -1;
	}
	p = virbuild_register_read(e, rp, VIR_TYPE_PTR);
	if (op != WALK_ATOMIC_LOAD)
		v = virbuild_register_read(e, rv, ty);
	virbuild_store_table_clear(e, true);
	switch (op) {
	case WALK_ATOMIC_LOAD: v = virbuild_insn_append(e, VIR_OP_ALOAD, ty, fl, p, 0, 0); break;
	case WALK_ATOMIC_STORE: virbuild_insn_append(e, VIR_OP_ASTORE, VIR_TYPE_VOID, fl, p, v, 0); return -1;
	case WALK_ATOMIC_EXCHANGE: v = virbuild_insn_append(e, VIR_OP_AXCHG, ty, fl, p, v, 0); break;
	case WALK_ATOMIC_ADD: v = virbuild_insn_append(e, VIR_OP_AXADD, ty, fl, p, v, 0); break;
	default: v = virbuild_insn_append(e, VIR_OP_ACAS, ty, fl, p, virbuild_register_read(e, rexp, ty), v); break;
	}
	r = virbuild_treg(e);
	virbuild_register_write(e, r, v);
	if (size < 4)	/* (the narrow value zero-extended, as the hardware's) */
		virbuild_ext(e, r, size, false);
	return r;
}

VPRIV int virbuild_tlsaddr(codegen_context *e, uint32_t ref, int model)
{
	if (!CODEGEN_IS_X64(e->compile_context) || model > 1 || (model && CODEGEN_IS_WIN64(e->compile_context)))
		virbuild_fail(e, VIRBUILD_FAILURE(thread_local));
	int r = virbuild_treg(e);
	uint32_t addr;
	if (model == 1) {
		uint32_t base = virbuild_constant_make(e, VIR_OP_SYMADDR, VIR_TYPE_PTR,
			CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_TLS_BASE, 0), 0, 0);
		uint32_t offset = virbuild_constant_make(e, VIR_OP_SYMADDR, VIR_TYPE_PTR,
			CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_TLS_OFFSET, CODEGEN_REFERENCE_INDEX(ref)), 0, 0);
		addr = virbuild_insn_append(e, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, base, virbuild_value_convert(e, offset, VIR_TYPE_I64), 0);
	} else {
		addr = virbuild_constant_make(e, VIR_OP_SYMADDR, VIR_TYPE_PTR,
			CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_TLS_LE, CODEGEN_REFERENCE_INDEX(ref)), 0, 0);
	}
	virbuild_register_write(e, r, addr);
	return r;
}
VPRIV codegen_value virbuild_frame(codegen_context *e, bool ret, uint32_t level) { (void)ret, (void)level; virbuild_fail(e, VIRBUILD_FAILURE(frame_address)); }
VPRIV codegen_value virbuild_alloca(codegen_context *e, codegen_value n) { (void)n; virbuild_fail(e, VIRBUILD_FAILURE(alloca)); }
VPRIV int virbuild_spget(codegen_context *e) { virbuild_fail(e, VIRBUILD_FAILURE(stack_pointer)); }
VPRIV void virbuild_spset(codegen_context *e, int r) { (void)r; virbuild_fail(e, VIRBUILD_FAILURE(stack_pointer)); }
VPRIV codegen_value virbuild_setjmp(codegen_context *e, int rbuf) { (void)rbuf; virbuild_fail(e, VIRBUILD_FAILURE(setjmp)); }
VPRIV void virbuild_longjmp(codegen_context *e, int rbuf) { (void)rbuf; virbuild_fail(e, VIRBUILD_FAILURE(longjmp)); }
/* A nonvolatile register-only CRC32Q has one tied accumulator and one
 * input. Keep both as SSA values so the x86 allocator sees the tie. */
VPRIV void virbuild_asm(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const ast_asm_statement *a = &c->asms[N(n)->a];
	if (!CODEGEN_IS_X64(c) || !a->extended || a->go || a->vol || a->nout != 1 || a->nin != 1 ||
	    a->nclob || a->nlabel || strcmp(c->strs[a->tmpl - 1].s, "crc32q %1, %0") ||
	    strcmp(c->strs[a->ops[0].constraint - 1].s, "+r") || strcmp(c->strs[a->ops[1].constraint - 1].s, "r"))
		virbuild_fail(e, VIRBUILD_FAILURE(inline_assembly));

	for (unsigned k = 0; k < 2; k++) {
		const node *operand = N(a->ops[k].expr);
		if (operand->op != AST_NODE_VARIABLE || !codegen_symbol_is_local_variable(c, operand->a) ||
		    c->syms[operand->a].sc == AST_STORAGE_REGISTER || AST_QUALIFIED_TYPE_QUALIFIERS(operand->ty) ||
		    !codegen_type_is_integer(c, operand->ty) || codegen_type_is_bitint(c, operand->ty) ||
		    walk_type_size(c, operand->ty) != 8)
			virbuild_fail(e, VIRBUILD_FAILURE(inline_assembly));
	}
	uint32_t out = a->ops[0].expr;
	codegen_lvalue dest = walk_lvalue_emit(e, out);
	codegen_value acc = walk_lvalue_reload(e, &dest, N(out)->ty);
	codegen_value word = walk_value_emit(e, a->ops[1].expr);
	uint32_t seed = virbuild_walker_value_get(e, &acc, VIR_TYPE_I64);
	uint32_t data = virbuild_walker_value_get(e, &word, VIR_TYPE_I64);
	int r = virbuild_result_register_pick(e, &acc, &word);
	virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_CRC32C64, VIR_TYPE_I64, 0, seed, data, 0));
	walk_lvalue_store_register(e, &dest, N(out)->ty, r);
	virbuild_tfree(e, r);
	if (dest.kind == CODEGEN_LVALUE_REGISTER) virbuild_tfree(e, dest.register_number);
}
/* the va_list at rap points at the first unnamed argument (Apple's: on the stack) */
VPRIV void virbuild_va_start(codegen_context *e, int rap)
{
	if (!CODEGEN_IS_DARWIN(e->compile_context))
		virbuild_fail(e, VIRBUILD_FAILURE(variadic_start));
	virbuild_insn_append(e, VIR_OP_VA_START, VIR_TYPE_VOID, 0, virbuild_register_read(e, rap, VIR_TYPE_PTR), 0, 0);
	virbuild_store_table_clear(e, true);
}
/* the address of the next argument of va_list *rap, which moves past it (as va_arg_ in
 * arch/a64/codegen/builtin.c); Apple's va_list is a pointer into the stack arguments,
 * 8 bytes each (16-aligned ones aligned first, records over 16 bytes by reference) */
VPRIV int virbuild_va_arg(codegen_context *e, int rap, ast_qualified_type t)
{
	cc *c = e->compile_context;
	uint64_t size = codegen_type_size(c, t);
	bool big = codegen_type_is_record_like(c, t) && size > 16;
	codegen_lvalue ap = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rap, .align = 8 };
	uint32_t p, q;
	int r, rn;

	if (!CODEGEN_IS_DARWIN(c))
		virbuild_fail(e, VIRBUILD_FAILURE(variadic_argument));
	r = virbuild_treg(e);
	virbuild_load(e, r, &ap, 8, false);
	p = virbuild_register_read(e, r, VIR_TYPE_PTR);
	if (size && !big && a64_abi_type_alignment(c, t) == 16) {
		q = virbuild_insn_append(e, VIR_OP_P2I, VIR_TYPE_I64, 0, p, 0, 0);
		q = virbuild_insn_append(e, VIR_OP_ADD, VIR_TYPE_I64, 0, q, virbuild_constant_integer(e, VIR_TYPE_I64, 15), 0);
		q = virbuild_insn_append(e, VIR_OP_AND, VIR_TYPE_I64, 0, q, virbuild_constant_integer(e, VIR_TYPE_I64, -16), 0);
		p = virbuild_insn_append(e, VIR_OP_I2P, VIR_TYPE_PTR, 0, q, 0, 0);
	}
	q = virbuild_insn_append(e, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, p, virbuild_constant_integer(e, VIR_TYPE_I64, big ? 8 : (int64_t)((size + 7) & ~(uint64_t)7)), 0);
	rn = virbuild_treg(e);
	virbuild_register_write(e, rn, q);
	virbuild_store(e, rn, &ap, 8);
	virbuild_tfree(e, rn);
	virbuild_register_write(e, r, p);
	if (big) {
		codegen_lvalue m = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)r, .align = 8 };

		virbuild_load(e, r, &m, 8, false);
	}
	return r;
}
VPRIV void virbuild_retrec(codegen_context *e, int raddr, const walk_argument *r, uint32_t lbl)
{
	codegen_lvalue at = { .kind = CODEGEN_LVALUE_FRAME, .offset = r->slot, .align = 8 };
	int q;
	if (r->is_in_memory) {
		q = virbuild_treg(e);
		virbuild_load(e, q, &at, 8, false);
		virbuild_copy(e, q, raddr, r->size);
	} else {
		for (int j = 0; j < r->eightbyte_count; j++) {
			uint32_t ty = r->classes[j] == WALK_ARGUMENT_INTEGER ? VIR_TYPE_I64 :
				r->size == 4 ? VIR_TYPE_F32 : VIR_TYPE_F64;
			q = ty == VIR_TYPE_I64 ? virbuild_treg(e) : virbuild_ftreg(e);
			at = (codegen_lvalue){ .kind = CODEGEN_LVALUE_REGISTER, .register_number = raddr, .offset = 8*j };
			virbuild_load(e, q, &at, ty == VIR_TYPE_F32 ? 4 : 8, false);
			uint32_t v = virbuild_register_read(e, q, ty);
			virbuild_variable_write(e, VIRBUILD_RESULT_VARIABLE + j, e->vir_builder->current_block, v);
			virbuild_tfree(e, q);
		}
		q = -1;
	}
	virbuild_tfree(e, q);
	virbuild_tfree(e, raddr);
	virbuild_branch_emit(e, virbuild_label_block(e, lbl));
}
VPRIV bool virbuild_tail_return(codegen_context *e, uint32_t expr) { (void)e, (void)expr; return false; }

VPRIV void virbuild_trap(codegen_context *e)
{
	virbuild_block_terminate(e, VIR_OP_TRAP, 0, 0, 0, 0);
}

VPRIV void virbuild_barrier(codegen_context *e)
{
	virbuild_insn_append(e, VIR_OP_BARRIER, VIR_TYPE_VOID, 0, 0, 0, 0);
	virbuild_store_table_clear(e, true);
}

VPRIV void virbuild_dbgrow(codegen_context *e, uint32_t loc) { e->current_location = loc; }

VPRIV void virbuild_label(codegen_context *e, uint32_t l)
{
	virbuild_context *V = e->vir_builder;
	uint32_t b = virbuild_label_block(e, l);

	e->is_reachable = true;
	V->blocks[V->current_block].terminator = virbuild_preir_insn_add(e, VIR_OP_BR, VIR_TYPE_VOID, 0, b, 0, 0, V->current_block);
	V->current_block = b;
	V->store_count = 0;
	walk_array_elements_clear(e, UINT32_MAX);
}

VPRIV void virbuild_jump(codegen_context *e, uint32_t l) { virbuild_branch_emit(e, virbuild_label_block(e, l)); }
VPRIV void virbuild_jumpr(codegen_context *e, int r)
{
	virbuild_context *V = e->vir_builder;
	if (!CODEGEN_IS_X64(e->compile_context) || e->walk_state->instance)
		virbuild_fail(e, VIRBUILD_FAILURE(computed_goto));
	if (V->indirect_dispatch == VIRBUILD_BLOCK_NONE)
		V->indirect_dispatch = virbuild_block_new(e);
	uint32_t target = virbuild_register_read(e, r, VIR_TYPE_PTR);
	virbuild_tfree(e, r);
	virbuild_variable_write(e, VIRBUILD_JUMP_VARIABLE, V->current_block, target);
	virbuild_branch_emit(e, V->indirect_dispatch);
}

VPRIV void virbuild_named(codegen_context *e, uint32_t ref)
{
	if (!CODEGEN_IS_X64(e->compile_context) || e->walk_state->instance)
		virbuild_fail(e, VIRBUILD_FAILURE(label_address));
	virbuild_context *V = e->vir_builder;
	VIRBUILD_ARRAY_GROW(V->indirect_targets, V->indirect_count, V->indirect_capacity, 1);
	V->indirect_targets[V->indirect_count].reference = ref;
	V->indirect_targets[V->indirect_count++].block = V->current_block;
}
