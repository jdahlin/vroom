/* vcc code generator, x86-64: the walker's operations as records (x64-internal.h) */
#include "x64-internal.h"

#define X64_RECORD_MAX (1u << 22)
#define X64_RECORD_BUFFER ((size_t)X64_RECORD_MAX * 3)	/* twoaddr.c adds at most two records per record; the buffers swap */

/* temporaries in the order they are handed out: the argument registers last */
static const uint8_t x64_order[8] = { R10, R9, R8, RDI, RSI, RDX, RCX, RAX };

VPRIV x64_lowering_context *x64_context_get(codegen_context *e)
{
	x64_lowering_context *X = e->x64_context;

	if (!X) {
		X = calloc(1, sizeof *X);
		X->in = codegen_memory_reserve(X64_RECORD_BUFFER * sizeof(record));
		X->tw = codegen_memory_reserve(X64_RECORD_BUFFER * sizeof(record));
		X->capin = X64_RECORD_MAX;
		e->x64_context = X;
	}
	return X;
}

VPRIV void x64_ecx_free(codegen_context *e)
{
	x64_lowering_context *X = e->x64_context;

	if (!X)
		return;
	munmap(X->in, X64_RECORD_BUFFER * sizeof(record));
	munmap(X->tw, X64_RECORD_BUFFER * sizeof(record));
	free(X);
	e->x64_context = NULL;
}

VPRIV record *x64_record_emit(codegen_context *e, int op)
{
	x64_lowering_context *X = e->x64_context;
	record *r;

	if (!e->is_reachable && op != X64_OP_LABEL) {
		/* unreachable code is not emitted (as arm64): it may name a string or a symbol
		   nobody claimed (note_ref skips them) */
		static _Thread_local record sink;

		memset(&sink, 0, sizeof sink);
		sink.opcode = (uint8_t)op;
		sink.registers[0] = sink.registers[1] = sink.registers[2] = sink.registers[3] = X64_REGISTER_NONE;
		return &sink;
	}
	if (unlikely(X->nin == X->capin))
		codegen_fatal_report(e, e->current_location, "function too large");
	r = &X->in[X->nin++];
	memset(r, 0, sizeof *r);
	r->opcode = (uint8_t)op;
	r->registers[0] = r->registers[1] = r->registers[2] = r->registers[3] = X64_REGISTER_NONE;
	return r;
}

VPRIV record *x64_record_emit_three_registers(codegen_context *e, int op, int sz, int r0, int r1, int r2)
{
	record *i = x64_record_emit(e, op);

	i->size = (uint8_t)sz, i->registers[0] = (uint8_t)r0, i->registers[1] = (uint8_t)r1, i->registers[2] = (uint8_t)r2;
	return i;
}

VPRIV void x64_mov(codegen_context *e, int rd, int rs)
{
	if (rd != rs)
		x64_record_emit_three_registers(e, X64_OP_MOV, 8, rd, rs, X64_REGISTER_NONE);
}

/* the base register of l's memory operand (ref: its symbol, RIP-relative); a
   symbol reached through the GOT (-fPIC/-fPIE, an undefined weak) is loaded into r11 */
VPRIV int x64_memory_base_register(codegen_context *e, const codegen_lvalue *l, uint32_t *ref)
{
	*ref = 0;
	if (l->kind == CODEGEN_LVALUE_FRAME)
		return e->x64_context->spframe ? X64_REGISTER_FRAME : RBP;
	if (l->kind != CODEGEN_LVALUE_SYMBOL)
		return l->register_number;
	if (codegen_symbol_needs_got(e->compile_context, l->reference)) {
		record *g = x64_record_emit(e, X64_OP_GOT);

		g->size = 8;
		g->registers[0] = R11;
		g->reference = l->reference;
		return R11;
	}
	*ref = l->reference;
	return RIP;
}

VPRIV void x64_record_emit_memory(codegen_context *e, int op, int sz, int r, const codegen_lvalue *l, bool sgn)
{
	/* x86 memory displacements are signed 32-bit, but C address offsets are
	   pointer-width. Materialize a full address before the final memory op. */
	if (l->offset < INT32_MIN || l->offset > INT32_MAX) {
		if (op == X64_OP_ST && r == R11 && l->kind == CODEGEN_LVALUE_SYMBOL && codegen_symbol_needs_got(e->compile_context, l->reference))
			codegen_fatal_report(e, e->current_location, "internal error: a store through the GOT from r11");
		uint32_t live = e->x64_context->live;
		if (r < 32) e->x64_context->live |= 1u << r;
		if (l->kind == CODEGEN_LVALUE_REGISTER && l->register_number < 32) e->x64_context->live |= 1u << l->register_number;
		int address = x64_treg(e), offset = x64_treg(e);
		codegen_lvalue origin = *l;
		/* A negative frame displacement marks a local for the printer's final
		   R12 realignment rewrite. Keep that marker in the address anchor. */
		origin.offset = l->kind == CODEGEN_LVALUE_FRAME && l->offset < 0 ? -1 : 0;
		x64_record_emit_memory(e, X64_OP_LEA, 8, address, &origin, false);
		x64_record_emit_move_immediate(e, offset, l->offset - origin.offset);
		x64_record_emit_three_registers(e, X64_OP_ADD, 8, address, address, offset);
		codegen_lvalue at = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)address };
		x64_record_emit_memory(e, op, sz, r, &at, sgn);
		e->x64_context->live = live;
		return;
	}
	uint32_t ref;
	int base = x64_memory_base_register(e, l, &ref);
	record *i;

	if (base == R11 && l->kind == CODEGEN_LVALUE_SYMBOL && op == X64_OP_ST && r == R11)
		codegen_fatal_report(e, e->current_location, "internal error: a store through the GOT from r11");
	i = x64_record_emit(e, op);

	i->size = (uint8_t)sz;
	i->condition = sgn;
	i->offset = (int32_t)l->offset;
	i->registers[1] = (uint8_t)base;
	i->reference = ref;
	if (op == X64_OP_ST)
		i->registers[2] = (uint8_t)r;
	else
		i->registers[0] = (uint8_t)r;
}

/* ---------------- registers ---------------- */

VPRIV int x64_treg(codegen_context *e)
{
	x64_lowering_context *X = e->x64_context;

	for (int k = 0; k < 8; k++)
		if (!(X->live & (1u << x64_order[k]))) {
			X->live |= 1u << x64_order[k];
			return x64_order[k];
		}
	if (X->asmcs) {
		/* an asm that clobbers every temporary (call_on_stack): its operands in callee-saved
		   registers, as clang (the prologue saves them) */
		static const int cs[] = { RBX, R12, R13, R14, R15 };

		for (int k = 0; k < 5; k++)
			if (!(X->live & (1u << cs[k]))) {
				X->live |= 1u << cs[k];
				x64_callee_save_use(e, cs[k]);
				return cs[k];
			}
	}
	codegen_fatal_report(e, e->current_location, "x86-64: expression too complex (out of registers)");
}

VPRIV void x64_tfree(codegen_context *e, int r)
{
	if (r < 32)
		e->x64_context->live &= ~(1u << r);
}

VPRIV void x64_tkeep(codegen_context *e, int r)
{
	if (r < 32)
		e->x64_context->live |= 1u << r;
}

VPRIV bool x64_target_has_no_simd(codegen_context *e)
{
	uint32_t fn = e->walk_state->inline_call ? e->walk_state->inline_call->function : e->walk_state->function_symbol;
	return !x86_feature_has(cc_x86_features_get(e->compile_context, fn), X86_FEATURE_SSE2);
}

VPRIV void x64_nofp(codegen_context *e)
{
	if (x64_target_has_no_simd(e))
		codegen_fatal_report(e, e->current_location, "x86-64: floating point with -mgeneral-regs-only or -mno-sse is not supported (PLAN-x86.md X3)");
}

VPRIV int x64_ftreg(codegen_context *e)
{
	x64_lowering_context *X = e->x64_context;

	x64_nofp(e);
	for (int r = XMM8; r < XMM15; r++)
		if (!(X->live & (1u << r))) {
			X->live |= 1u << r;
			return r;
		}
	codegen_fatal_report(e, e->current_location, "x86-64: expression too complex (out of vector registers)");
}

VPRIV int x64_nfree(codegen_context *e) { return 8 - __builtin_popcount(e->x64_context->live & X64_TEMPORARIES); }
VPRIV int x64_nffree(codegen_context *e) { return XMM15 - XMM8 - __builtin_popcount(e->x64_context->live & X64_FLOAT_TEMPORARIES); }

VPRIV int x64_toreg(codegen_context *e, codegen_value *v, int size)
{
	int r;

	(void)size;
	if (v->kind == CODEGEN_VALUE_REGISTER)
		return v->register_number;
	r = x64_treg(e);
	if (v->kind == CODEGEN_VALUE_CONSTANT) {
		record *i = x64_record_emit_three_registers(e, X64_OP_MOVI, 8, r, X64_REGISTER_NONE, X64_REGISTER_NONE);

		i->immediate = (int64_t)v->integer;
	} else {
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_SYMBOL, .reference = v->reference, .offset = (int64_t)v->integer };

		x64_record_emit_memory(e, X64_OP_LEA, 8, r, &l, false);
	}
	*v = (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r };
	return r;
}

/* ---------------- memory ---------------- */

VPRIV void x64_load(codegen_context *e, int rd, const codegen_lvalue *l, int size, bool sgn)
{
	if (l->kind == CODEGEN_LVALUE_VARIABLE)
		codegen_fatal_report(e, e->current_location, "internal error: x86-64 load from a virtual variable");
	if (l->kind == CODEGEN_LVALUE_LOCAL_REGISTER) {
		if (size < 8)
			x64_record_emit_three_registers(e, X64_OP_EXT, size, rd, l->register_number, X64_REGISTER_NONE)->condition = sgn;
		else
			x64_mov(e, rd, l->register_number);
	} else if (size == 2 && X64_REGISTER_IS_XMM(rd)) {
		x64_record_emit_memory(e, X64_OP_LD, 2, R11, l, false);
		x64_record_emit_three_registers(e, X64_OP_MOV, 4, rd, R11, X64_REGISTER_NONE);
	} else
		x64_record_emit_memory(e, X64_OP_LD, size, rd, l, sgn);
}

VPRIV void x64_store(codegen_context *e, int rs, const codegen_lvalue *l, int size)
{
	if (l->kind == CODEGEN_LVALUE_VARIABLE)
		codegen_fatal_report(e, e->current_location, "internal error: x86-64 store to a virtual variable");
	if (l->kind == CODEGEN_LVALUE_LOCAL_REGISTER) {
		x64_mov(e, l->register_number, rs);
	} else if (size == 2 && X64_REGISTER_IS_XMM(rs)) {
		x64_record_emit_three_registers(e, X64_OP_MOV, 4, R11, rs, X64_REGISTER_NONE);
		x64_record_emit_memory(e, X64_OP_ST, 2, R11, l, false);
	} else
		x64_record_emit_memory(e, X64_OP_ST, size, rs, l, false);
}

VPRIV int x64_laddr(codegen_context *e, const codegen_lvalue *l)
{
	if (l->kind == CODEGEN_LVALUE_VARIABLE)
		codegen_fatal_report(e, e->current_location, "internal error: address of a virtual variable");
	if (l->kind == CODEGEN_LVALUE_LOCAL_REGISTER)
		codegen_fatal_report(e, e->current_location, "internal error: address of a register local");
	int r = l->kind == CODEGEN_LVALUE_REGISTER ? l->register_number : x64_treg(e);

	if (l->kind != CODEGEN_LVALUE_REGISTER || l->offset)
		x64_record_emit_memory(e, X64_OP_LEA, 8, r, l, false);
	return r;
}

/* registers a block operation needs (rep movsb/stosb), saved if live and not one of keep */
VPRIV uint32_t x64_save(codegen_context *e, const uint8_t *regs, int n, int k0, int k1)
{
	x64_lowering_context *X = e->x64_context;
	uint32_t sv = 0;

	for (int k = 0; k < n; k++)
		if ((X->live & (1u << regs[k])) && regs[k] != k0 && regs[k] != k1)
			sv |= 1u << regs[k];
	/* x64_restore pops in descending register order, independent of the
	   caller's list order. Match it even when several registers are live. */
	for (int r = 0; r < 16; r++)
		if (sv & (1u << r)) {
			x64_record_emit_three_registers(e, X64_OP_PUSH, 8, X64_REGISTER_NONE, r, X64_REGISTER_NONE);
			X->pushes++;
		}
	return sv;
}

VPRIV void x64_restore(codegen_context *e, uint32_t sv)
{
	x64_lowering_context *X = e->x64_context;

	for (int r = 15; r >= 0; r--)
		if (sv & (1u << r)) {
			x64_record_emit_three_registers(e, X64_OP_POP, 8, r, X64_REGISTER_NONE, X64_REGISTER_NONE);
			X->pushes--;
		}
}

VPRIV void x64_record_emit_move_immediate(codegen_context *e, int r, int64_t v)
{
	x64_record_emit_three_registers(e, X64_OP_MOVI, 8, r, X64_REGISTER_NONE, X64_REGISTER_NONE)->immediate = v;
}

VPRIV void x64_copy(codegen_context *e, int rdst, int rsrc, uint64_t n)
{
	if (n <= 128) {
		for (uint64_t o = 0; o < n;) {
			int k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;
			codegen_lvalue s = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rsrc, .offset = (int64_t)o }, d = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rdst, .offset = (int64_t)o };

			x64_record_emit_memory(e, X64_OP_LD, k, R11, &s, false);
			x64_record_emit_memory(e, X64_OP_ST, k, R11, &d, false);
			o += (uint64_t)k;
		}
		return;
	}
	{
		static const uint8_t rr[3] = { RDI, RSI, RCX };
		uint32_t sv = x64_save(e, rr, 3, -1, -1);

		/* rdst, rsrc may be any of rdi rsi rcx: through r11 and the stack */
		x64_record_emit_three_registers(e, X64_OP_PUSH, 8, X64_REGISTER_NONE, rsrc, X64_REGISTER_NONE);
		x64_mov(e, R11, rdst);
		x64_record_emit_three_registers(e, X64_OP_POP, 8, RSI, X64_REGISTER_NONE, X64_REGISTER_NONE);
		x64_mov(e, RDI, R11);
		x64_record_emit_move_immediate(e, RCX, (int64_t)n);
		x64_record_emit(e, X64_OP_REP)->condition = 0;
		x64_restore(e, sv);
	}
}

VPRIV void x64_zero(codegen_context *e, int rdst, uint64_t n)
{
	if (n <= 128) {
		x64_record_emit_three_registers(e, X64_OP_MOVI, 4, R11, X64_REGISTER_NONE, X64_REGISTER_NONE)->immediate = 0;
		for (uint64_t o = 0; o < n;) {
			int k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;
			codegen_lvalue d = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rdst, .offset = (int64_t)o };

			x64_record_emit_memory(e, X64_OP_ST, k, R11, &d, false);
			o += (uint64_t)k;
		}
		return;
	}
	{
		static const uint8_t rr[3] = { RDI, RAX, RCX };
		uint32_t sv = x64_save(e, rr, 3, -1, -1);

		x64_mov(e, RDI, rdst);
		x64_record_emit_three_registers(e, X64_OP_MOVI, 4, RAX, X64_REGISTER_NONE, X64_REGISTER_NONE)->immediate = 0;
		x64_record_emit_move_immediate(e, RCX, (int64_t)n);
		x64_record_emit(e, X64_OP_REP)->condition = 1;
		x64_restore(e, sv);
	}
}

VPRIV void x64_ext(codegen_context *e, int r, int from, bool sgn)
{
	record *i;

	if (from >= 8)
		return;
	if (from == 4 && !sgn && e->unit->options->opt >= 1 && !e->unit->cleanups_disabled && x64_last_u32(e, r))
		return;
	i = x64_record_emit_three_registers(e, X64_OP_EXT, from, r, r, X64_REGISTER_NONE);
	i->condition = sgn;
}

/* ---------------- integer operations ---------------- */

static bool x64_imm32(codegen_value v) { return v.kind == CODEGEN_VALUE_CONSTANT && v.integer >= INT32_MIN && v.integer <= INT32_MAX; }

/* rax, rdx: saved around div if something else lives there */
static codegen_value x64_div(codegen_context *e, int op, int size, bool sgn, codegen_value a, codegen_value b)
{
	x64_lowering_context *X = e->x64_context;
	int ra = x64_toreg(e, &a, size), rb = x64_toreg(e, &b, size), d = rb;
	bool sv[2] = { false, false };
	static const uint8_t fx[2] = { RAX, RDX };

	for (int k = 0; k < 2; k++)
		if ((X->live & (1u << fx[k])) && fx[k] != ra && fx[k] != rb) {
			x64_record_emit_three_registers(e, X64_OP_PUSH, 8, X64_REGISTER_NONE, fx[k], X64_REGISTER_NONE);
			X->pushes++;
			sv[k] = true;
		}
	if (rb == RAX || rb == RDX) {
		x64_mov(e, R11, rb);
		d = R11;
	}
	x64_mov(e, RAX, ra);
	if (sgn) {
		x64_record_emit_three_registers(e, X64_OP_CQO, size, RDX, RAX, X64_REGISTER_NONE);
	} else {
		record *z = x64_record_emit_three_registers(e, X64_OP_MOVI, 4, RDX, X64_REGISTER_NONE, X64_REGISTER_NONE);

		z->immediate = 0;
	}
	x64_record_emit_three_registers(e, sgn ? X64_OP_IDIV : X64_OP_DIV, size, X64_REGISTER_NONE, d, X64_REGISTER_NONE);
	x64_mov(e, ra, op == LEX_TOKEN_SLASH ? RAX : RDX);
	for (int k = 1; k >= 0; k--)
		if (sv[k]) {
			x64_record_emit_three_registers(e, X64_OP_POP, 8, fx[k], X64_REGISTER_NONE, X64_REGISTER_NONE);
			X->pushes--;
		}
	x64_tfree(e, rb);
	return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)ra };
}

/* a shift by a register (op: X64_OP_SHL X64_OP_SHR X64_OP_SAR): the count in cl */
static codegen_value x64_shift(codegen_context *e, int op, int size, codegen_value a, codegen_value b)
{
	x64_lowering_context *X = e->x64_context;
	int ra = x64_toreg(e, &a, size), rb = x64_toreg(e, &b, size), dst = ra == RCX ? R11 : ra;
	bool sv = false;

	if (dst == R11)
		x64_mov(e, R11, ra);
	if (rb != RCX) {
		if ((X->live & (1u << RCX)) && ra != RCX) {
			x64_record_emit_three_registers(e, X64_OP_PUSH, 8, X64_REGISTER_NONE, RCX, X64_REGISTER_NONE);
			X->pushes++;
			sv = true;
		}
		x64_mov(e, RCX, rb);
	}
	x64_record_emit_three_registers(e, op, size, dst, dst, RCX);
	if (dst == R11)
		x64_mov(e, ra, R11);
	if (sv) {
		x64_record_emit_three_registers(e, X64_OP_POP, 8, RCX, X64_REGISTER_NONE, X64_REGISTER_NONE);
		X->pushes--;
	}
	if (rb != ra)
		x64_tfree(e, rb);
	return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)ra };
}

VPRIV codegen_value x64_bin(codegen_context *e, int op, int size, bool sgn, codegen_value a, codegen_value b)
{
	int ra, rb, xop, xopi;

	switch (op) {
	case LEX_TOKEN_PLUS: xop = X64_OP_ADD, xopi = X64_OP_ADDI; break;
	case LEX_TOKEN_MINUS: xop = X64_OP_SUB, xopi = X64_OP_SUBI; break;
	case LEX_TOKEN_AMPERSAND: xop = X64_OP_AND, xopi = X64_OP_ANDI; break;
	case LEX_TOKEN_PIPE: xop = X64_OP_OR, xopi = X64_OP_ORI; break;
	case LEX_TOKEN_CARET: xop = X64_OP_XOR, xopi = X64_OP_XORI; break;
	case LEX_TOKEN_STAR: xop = X64_OP_IMUL, xopi = X64_OP_IMULI; break;
	case LEX_TOKEN_SLASH: case LEX_TOKEN_PERCENT:
		return x64_div(e, op, size, sgn, a, b);
	case LEX_TOKEN_LESS_LESS: case LEX_TOKEN_GREATER_GREATER:
		if (b.kind == CODEGEN_VALUE_CONSTANT) {
			record *i;

			ra = x64_toreg(e, &a, size);
			i = x64_record_emit_three_registers(e, op == LEX_TOKEN_LESS_LESS ? X64_OP_SHLI : sgn ? X64_OP_SARI : X64_OP_SHRI, size, ra, ra, X64_REGISTER_NONE);
			i->immediate = (int64_t)(b.integer & (size * 8 - 1));
			return a;
		}
		return x64_shift(e, op == LEX_TOKEN_LESS_LESS ? X64_OP_SHL : sgn ? X64_OP_SAR : X64_OP_SHR, size, a, b);
	default:
		codegen_fatal_report(e, e->current_location, "x86-64: operator %d", op);
	}
	/* a constant on the left of a commutative operation goes right */
	if (a.kind != CODEGEN_VALUE_REGISTER && b.kind == CODEGEN_VALUE_REGISTER && op != LEX_TOKEN_MINUS) {
		codegen_value t = a;

		a = b, b = t;
	}
	ra = x64_toreg(e, &a, size);
	if (op == LEX_TOKEN_STAR && b.kind == CODEGEN_VALUE_CONSTANT && x64_mulconst(e, size, ra, b.integer))
		return a;
	if (x64_imm32(b)) {
		record *i = x64_record_emit_three_registers(e, xopi, size, ra, ra, X64_REGISTER_NONE);

		i->immediate = (int64_t)b.integer;
		return a;
	}
	rb = x64_toreg(e, &b, size);
	x64_record_emit_three_registers(e, xop, size, ra, ra, rb);
	if (rb != ra)
		x64_tfree(e, rb);
	return a;
}

VPRIV codegen_value x64_un(codegen_context *e, int op, int size, codegen_value a)
{
	int ra = x64_toreg(e, &a, size);

	x64_record_emit_three_registers(e, op == LEX_TOKEN_MINUS ? X64_OP_NEG : X64_OP_NOT, size, ra, ra, X64_REGISTER_NONE);
	return a;
}

/* ---------------- comparisons ---------------- */

static int x64_cc(int op, bool sgn)
{
	switch (op) {
	case LEX_TOKEN_LESS: return sgn ? X64_CONDITION_L : X64_CONDITION_B;
	case LEX_TOKEN_LESS_EQUAL: return sgn ? X64_CONDITION_LE : X64_CONDITION_BE;
	case LEX_TOKEN_GREATER: return sgn ? X64_CONDITION_G : X64_CONDITION_A;
	case LEX_TOKEN_GREATER_EQUAL: return sgn ? X64_CONDITION_GE : X64_CONDITION_AE;
	case LEX_TOKEN_EQUAL_EQUAL: return X64_CONDITION_E;
	default: return X64_CONDITION_NE;
	}
}

static int x64_swapop(int op)
{
	return op == LEX_TOKEN_LESS ? LEX_TOKEN_GREATER : op == LEX_TOKEN_LESS_EQUAL ? LEX_TOKEN_GREATER_EQUAL : op == LEX_TOKEN_GREATER ? LEX_TOKEN_LESS : op == LEX_TOKEN_GREATER_EQUAL ? LEX_TOKEN_LESS_EQUAL : op;
}

/* flags for a op b; the condition code, and a's register (still live) */
static int x64_flags(codegen_context *e, int op, int size, bool sgn, codegen_value *a, codegen_value *b, int *ra)
{
	if (a->kind != CODEGEN_VALUE_REGISTER && b->kind == CODEGEN_VALUE_REGISTER) {
		codegen_value t = *a;

		*a = *b, *b = t;
		op = x64_swapop(op);
	}
	*ra = x64_toreg(e, a, size);
	if (x64_imm32(*b) && b->integer == 0 && (op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL)) {
		x64_record_emit_three_registers(e, X64_OP_TEST, size, X64_REGISTER_NONE, *ra, X64_REGISTER_NONE);
	} else if (x64_imm32(*b)) {
		record *i = x64_record_emit_three_registers(e, X64_OP_CMPI, size, X64_REGISTER_NONE, *ra, X64_REGISTER_NONE);

		i->immediate = (int64_t)b->integer;
	} else {
		int rb = x64_toreg(e, b, size);

		x64_record_emit_three_registers(e, X64_OP_CMP, size, X64_REGISTER_NONE, *ra, rb);
		if (rb != *ra)
			x64_tfree(e, rb);
	}
	return x64_cc(op, sgn);
}

VPRIV codegen_value x64_cmp(codegen_context *e, int op, int size, bool sgn, codegen_value a, codegen_value b)
{
	int ra, cc = x64_flags(e, op, size, sgn, &a, &b, &ra);
	record *i = x64_record_emit_three_registers(e, X64_OP_SETCC, 4, ra, X64_REGISTER_NONE, X64_REGISTER_NONE);

	i->condition = (uint8_t)cc;
	return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)ra };
}

VPRIV void x64_cmpbr(codegen_context *e, int op, int size, bool sgn, codegen_value a, codegen_value b, uint32_t lbl)
{
	int ra, cc = x64_flags(e, op, size, sgn, &a, &b, &ra);
	record *i = x64_record_emit(e, X64_OP_JCC);

	i->condition = (uint8_t)cc;
	i->label = lbl;
	x64_tfree(e, ra);
}

VPRIV void x64_brz(codegen_context *e, codegen_value a, int size, bool nz, uint32_t lbl)
{
	int ra = x64_toreg(e, &a, size);
	record *i;

	x64_record_emit_three_registers(e, X64_OP_TEST, size, X64_REGISTER_NONE, ra, X64_REGISTER_NONE);
	i = x64_record_emit(e, X64_OP_JCC);
	i->condition = nz ? X64_CONDITION_NE : X64_CONDITION_E;
	i->label = lbl;
	x64_tfree(e, ra);
}

/* ---------------- control ---------------- */

/* -g: a line-table row at the next record (arm64's codegen_debug_row_add, on x86's records) */
VPRIV void x64_dbgrow(codegen_context *e, uint32_t loc)
{
	uint32_t file, line, col, nin = e->x64_context->nin;

	if (!codegen_debug_location_get(e, loc, &file, &line, &col))
		return;
	if (e->debug_row_count && e->debug_rows[e->debug_row_count - 1].record_index >= nin)
		e->debug_row_count--;	/* nothing emitted for the previous one */
	if (e->debug_row_count == e->debug_row_capacity) {
		e->debug_row_capacity = e->debug_row_capacity ? 2 * e->debug_row_capacity : 256;
		e->debug_rows = realloc(e->debug_rows, e->debug_row_capacity * sizeof *e->debug_rows);
	}
	e->debug_rows[e->debug_row_count++] = (struct codegen_debug_row){ nin, file, line, col };
}

VPRIV void x64_label(codegen_context *e, uint32_t l)
{
	e->is_reachable = true;
	x64_record_emit(e, X64_OP_LABEL)->label = l;
	walk_array_elements_clear(e, UINT32_MAX);	/* (other edges come in: element values unknown) */	/* (the walker skips label-free statements after a jump) */
}
VPRIV void x64_jump(codegen_context *e, uint32_t l) { x64_record_emit(e, X64_OP_JMP)->label = l; }
VPRIV void x64_jumpr(codegen_context *e, int r) { x64_record_emit_three_registers(e, X64_OP_JMPR, 8, X64_REGISTER_NONE, r, X64_REGISTER_NONE); x64_tfree(e, r); }
VPRIV void x64_named(codegen_context *e, uint32_t ref) { x64_record_emit(e, X64_OP_NAMED)->reference = ref; }

/* ---------------- the function ---------------- */

VPRIV void x64_begin(codegen_context *e, uint32_t s)
{
	x64_lowering_context *X = x64_context_get(e);

	(void)s;
	X->nin = 0;
	X->spframe = false;
	X->framesz = X->spdelta = 0;
	X->live = 0;
	X->pushes = 0;
	e->discarded_scalar = false;
	memset(X->csslot, 0, sizeof X->csslot);
	e->debug_row_count = 0;
	e->raw.n = 0;
	e->asm_statement_count = 0;
}

typedef struct x64_compact_debug {
	codegen_context *e;
	uint32_t cursor;
} x64_compact_debug;

static void x64_compact_remap(void *ctx, uint32_t old, uint32_t next)
{
	x64_compact_debug *d = ctx;

	codegen_debug_rows_remap(d->e, &d->cursor, old, next);
}

VPRIV void x64_finish(codegen_context *e, uint32_t s)
{
	static const record_layout layout = {
		sizeof(record), offsetof(record, opcode), offsetof(record, label), X64_OP_JMP, X64_OP_LABEL,
	};
	tbuf *b = &e->out;
	size_t start;
	x64_compact_debug d = { e, 0 };

	if (e->x64_context->live)
		codegen_fatal_report(e, e->current_location, "internal error: x86-64 temporaries left live (%#x)", e->x64_context->live);
	if (e->unit->options->opt >= 1 && !e->unit->cleanups_disabled)
		e->x64_context->nin = record_unreachable_remove(e->x64_context->in, e->x64_context->nin, sizeof(record), offsetof(record, opcode),
						x64_flow_table(), X64_OP_COUNT, e->debug_row_count ? x64_compact_remap : NULL, &d);
	d.cursor = 0;
	if (e->unit->options->opt >= 1)
		e->x64_context->nin = record_fallthrough_drop(e->x64_context->in, e->x64_context->nin, &layout,
					       e->debug_row_count ? x64_compact_remap : NULL, &d);
	if (e->unit->options->opt >= 1 && !e->unit->cleanups_disabled)
		x64_dead(e);
	x64_two_address_convert(e);
	b->n = (b->n + 7) & ~(size_t)7;
	start = b->n;
	codegen_symbol_header_append(e, b, s, true, "\t.text\n", 16);
	x64_function_print(e, b, s);
	size_t text = codegen_item_open(e, CODEGEN_ITEM_TEXT);
	if (e->unit->options->pfe_n) {
		/* the patchable entry's address (the M nops before it, else the entry itself) */
		vtext_string_append(b, "\t.pushsection\t__patchable_function_entries,\"awo\",@progbits,");
		codegen_symbol_name_append(e->compile_context, b, s);
		vtext_string_append(b, "\n\t.p2align\t3\n\t.quad\t");
		if (e->unit->options->pfe_m) {
			vtext_string_append(b, ".Lpfe");
			vtext_unsigned_append(b, s);
		} else {
			codegen_symbol_name_append(e->compile_context, b, s);
		}
		vtext_string_append(b, "\n\t.popsection\n");
	}
	{
		const attrs *a = codegen_symbol_attributes(e->compile_context, s);

		if (a->constructor)
			codegen_constructor_entry_append(e->compile_context, b, s, false);
		if (a->destructor)
			codegen_constructor_entry_append(e->compile_context, b, s, true);
	}
	codegen_item_close(e, text);
	e->unit->function_total++;
	em_switch_values_emit(e);
	codegen_chunk_add(e, (uint64_t)s << 1, start);
}

/* ---------------- builtins ---------------- */

VPRIV int x64_tlsaddr(codegen_context *e, uint32_t ref, int model)
{
	x64_lowering_context *X = e->x64_context;
	int r;
	record *i;

	if (model < 2) {
		r = x64_treg(e);
		i = x64_record_emit(e, X64_OP_TLS);
		i->registers[0] = (uint8_t)r;
		i->reference = ref;
		i->condition = (uint8_t)model;
		return r;
	}
	/* general dynamic: a call; the live temporaries saved around it, rsp 16-aligned */
	{
		uint32_t saved = X->live & X64_TEMPORARIES, fsaved = X->live & X64_FLOAT_TEMPORARIES;
		int pad;

		for (int q = 0; q < 16; q++)
			if (saved & (1u << q)) {
				x64_record_emit_three_registers(e, X64_OP_PUSH, 8, X64_REGISTER_NONE, q, X64_REGISTER_NONE);
				X->pushes++;
			}
		for (int q = XMM8; q < XMM15; q++)
			if (fsaved & (1u << q)) {
				codegen_lvalue d = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP };

				x64_record_emit(e, X64_OP_ADJSP)->immediate = -8;
				x64_record_emit_memory(e, X64_OP_ST, 8, q, &d, false);
				X->pushes++;
			}
		pad = X->pushes % 2 ? 8 : 0;
		if (pad)
			x64_record_emit(e, X64_OP_ADJSP)->immediate = -pad;
		i = x64_record_emit(e, X64_OP_TLS);
		i->registers[0] = RAX;
		i->reference = ref;
		i->condition = 2;
		if (pad)
			x64_record_emit(e, X64_OP_ADJSP)->immediate = pad;
		x64_mov(e, R11, RAX);
		for (int q = XMM15 - 1; q >= XMM8; q--)
			if (fsaved & (1u << q)) {
				codegen_lvalue s = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP };

				x64_record_emit_memory(e, X64_OP_LD, 8, q, &s, false);
				x64_record_emit(e, X64_OP_ADJSP)->immediate = 8;
				X->pushes--;
			}
		for (int q = 15; q >= 0; q--)
			if (saved & (1u << q)) {
				x64_record_emit_three_registers(e, X64_OP_POP, 8, q, X64_REGISTER_NONE, X64_REGISTER_NONE);
				X->pushes--;
			}
		r = x64_treg(e);
		x64_mov(e, r, R11);
		return r;
	}
}

/* ---------------- atomics (sequentially consistent: x86 orders the rest) ---------------- */

static record *x64_atrec(codegen_context *e, int op, int size, int r, int rp)
{
	record *i = x64_record_emit(e, op);

	i->size = (uint8_t)size;
	i->registers[0] = (uint8_t)r;
	i->registers[1] = (uint8_t)rp;
	return i;
}

VPRIV int x64_atomic(codegen_context *e, int op, int size, int rp, int rv, int rexp)
{
	x64_lowering_context *X = e->x64_context;
	int r;

	switch (op) {
	case WALK_ATOMIC_FENCE:
		x64_record_emit(e, X64_OP_MFENCE);
		return -1;
	case WALK_ATOMIC_LOAD: {
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rp };

		r = x64_treg(e);
		x64_record_emit_memory(e, X64_OP_LD, size, r, &l, false);	/* a plain load is sequentially consistent */
		return r;
	}
	case WALK_ATOMIC_STORE:
		x64_mov(e, R11, rv);
		x64_atrec(e, X64_OP_XCHG, size, R11, rp);
		return -1;
	case WALK_ATOMIC_EXCHANGE: case WALK_ATOMIC_ADD:
		r = x64_treg(e);
		x64_mov(e, r, rv);
		x64_atrec(e, op == WALK_ATOMIC_EXCHANGE ? X64_OP_XCHG : X64_OP_XADD, size, r, rp);
		x64_ext(e, r, size, false);
		return r;
	default: {	/* WALK_ATOMIC_COMPARE_EXCHANGE: rax is the comparand (saved if it holds something) */
		bool save = (X->live & (1u << RAX)) != 0;
		record *i;

		if (save) {
			x64_record_emit_three_registers(e, X64_OP_PUSH, 8, X64_REGISTER_NONE, RAX, X64_REGISTER_NONE);
			X->pushes++;
		}
		if (rv == RAX || rp == RAX) {
			x64_mov(e, R11, RAX);
			rv = rv == RAX ? R11 : rv;
			rp = rp == RAX ? R11 : rp;
		}
		x64_mov(e, RAX, rexp);
		i = x64_atrec(e, X64_OP_CMPXCHG, size, X64_REGISTER_NONE, rp);
		i->registers[2] = (uint8_t)rv;
		if (save) {
			x64_mov(e, R11, RAX);
			x64_record_emit_three_registers(e, X64_OP_POP, 8, RAX, X64_REGISTER_NONE, X64_REGISTER_NONE);
			X->pushes--;
			r = x64_treg(e);
			x64_mov(e, r, R11);
		} else {
			x64_tkeep(e, RAX);
			r = RAX;
		}
		x64_ext(e, r, size, false);	/* (cmpxchg leaves the upper bits as they were) */
		return r;
	}
	}
}

VPRIV void x64_trap(codegen_context *e) { x64_record_emit(e, X64_OP_UD2); }
VPRIV void x64_barrier(codegen_context *e) { x64_record_emit(e, X64_OP_BARRIER); }

VPRIV codegen_value x64_frame(codegen_context *e, bool ret, uint32_t level)
{
	int r = x64_treg(e);
	codegen_lvalue l = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)r };

	x64_mov(e, r, RBP);
	if (level <= 4) {
		for (uint32_t k = 0; k < level; k++)
			x64_record_emit_memory(e, X64_OP_LD, 8, r, &l, false);
	} else {
		/* Keep code size bounded even for a large constant frame depth. */
		int count = x64_treg(e);
		uint32_t loop = walk_label_new(e);
		record *j;

		x64_record_emit_move_immediate(e, count, level);
		x64_label(e, loop);
		x64_record_emit_memory(e, X64_OP_LD, 8, r, &l, false);
		x64_record_emit_three_registers(e, X64_OP_SUBI, 4, count, count, X64_REGISTER_NONE)->immediate = 1;
		j = x64_record_emit(e, X64_OP_JCC);
		j->condition = X64_CONDITION_NE;
		j->label = loop;
		x64_tfree(e, count);
	}
	if (ret) {
		l.offset = 8;
		x64_record_emit_memory(e, X64_OP_LD, 8, r, &l, false);
	}
	return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r };
}

/* a's bits counted SWAR-style (no popcnt in the x86-64 baseline): the count in a's register */
static void x64_popcount(codegen_context *e, int r, int size)
{
	int t = x64_treg(e);

	/* r -= (r >> 1) & 0x55..; r = (r & 0x33..) + ((r >> 2) & 0x33..); r = (r + (r >> 4)) & 0x0f..; r = (r * 0x01..) >> (size * 8 - 8) */
	x64_mov(e, t, r);
	x64_record_emit_three_registers(e, X64_OP_SHRI, 8, t, t, X64_REGISTER_NONE)->immediate = 1;
	x64_record_emit_move_immediate(e, R11, 0x5555555555555555);
	x64_record_emit_three_registers(e, X64_OP_AND, 8, t, t, R11);
	x64_record_emit_three_registers(e, X64_OP_SUB, 8, r, r, t);
	x64_mov(e, t, r);
	x64_record_emit_three_registers(e, X64_OP_SHRI, 8, t, t, X64_REGISTER_NONE)->immediate = 2;
	x64_record_emit_move_immediate(e, R11, 0x3333333333333333);
	x64_record_emit_three_registers(e, X64_OP_AND, 8, t, t, R11);
	x64_record_emit_three_registers(e, X64_OP_AND, 8, r, r, R11);
	x64_record_emit_three_registers(e, X64_OP_ADD, 8, r, r, t);
	x64_mov(e, t, r);
	x64_record_emit_three_registers(e, X64_OP_SHRI, 8, t, t, X64_REGISTER_NONE)->immediate = 4;
	x64_record_emit_three_registers(e, X64_OP_ADD, 8, r, r, t);
	x64_record_emit_move_immediate(e, R11, 0x0f0f0f0f0f0f0f0f);
	x64_record_emit_three_registers(e, X64_OP_AND, 8, r, r, R11);
	x64_record_emit_move_immediate(e, R11, 0x0101010101010101);
	x64_record_emit_three_registers(e, X64_OP_IMUL, 8, r, r, R11);
	x64_record_emit_three_registers(e, X64_OP_SHRI, 8, r, r, X64_REGISTER_NONE)->immediate = 56;
	x64_tfree(e, t);
	(void)size;
}

VPRIV codegen_value x64_bitop(codegen_context *e, int bi, int size, codegen_value a)
{
	int r = x64_toreg(e, &a, size), bits = size * 8;

	if (size == 4)
		x64_ext(e, r, 4, false);	/* (the count sees the 32 bits only) */
	switch (bi) {
	case CC_BUILTIN_CLZ: case CC_BUILTIN_CLZL: case CC_BUILTIN_CLZLL:
		x64_record_emit_three_registers(e, X64_OP_BSR, size, r, r, X64_REGISTER_NONE);
		x64_record_emit_three_registers(e, X64_OP_XORI, 4, r, r, X64_REGISTER_NONE)->immediate = bits - 1;
		break;
	case CC_BUILTIN_CTZ: case CC_BUILTIN_CTZL: case CC_BUILTIN_CTZLL:
		x64_record_emit_three_registers(e, X64_OP_BSF, size, r, r, X64_REGISTER_NONE);
		break;
	case CC_BUILTIN_FFS: case CC_BUILTIN_FFSL: case CC_BUILTIN_FFSLL: {
		/* x ? ctz(x) + 1 : 0 */
		uint32_t lz = walk_label_new(e);
		record *j;

		x64_record_emit_three_registers(e, X64_OP_TEST, size, X64_REGISTER_NONE, r, X64_REGISTER_NONE);
		j = x64_record_emit(e, X64_OP_JCC);
		j->condition = X64_CONDITION_E;
		j->label = lz;
		x64_record_emit_three_registers(e, X64_OP_BSF, size, r, r, X64_REGISTER_NONE);
		x64_record_emit_three_registers(e, X64_OP_ADDI, 4, r, r, X64_REGISTER_NONE)->immediate = 1;
		x64_label(e, lz);
		break;
	}
	case CC_BUILTIN_POPCOUNT: case CC_BUILTIN_POPCOUNTL: case CC_BUILTIN_POPCOUNTLL:
		x64_popcount(e, r, size);
		break;
	case CC_BUILTIN_PARITY: case CC_BUILTIN_PARITYL: case CC_BUILTIN_PARITYLL:
		x64_popcount(e, r, size);
		x64_record_emit_three_registers(e, X64_OP_ANDI, 4, r, r, X64_REGISTER_NONE)->immediate = 1;
		break;
	case CC_BUILTIN_BSWAP16:
		x64_record_emit_three_registers(e, X64_OP_BSWAP, 4, r, r, X64_REGISTER_NONE);
		x64_record_emit_three_registers(e, X64_OP_SHRI, 4, r, r, X64_REGISTER_NONE)->immediate = 16;
		break;
	case CC_BUILTIN_BSWAP32: case CC_BUILTIN_BSWAP64:
		x64_record_emit_three_registers(e, X64_OP_BSWAP, size, r, r, X64_REGISTER_NONE);
		break;
	default:
		codegen_fatal_report(e, e->current_location, "x86-64: builtin %d is not supported yet (PLAN-x86.md X3)", bi);
	}
	return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r };
}

/* n bytes below the stack pointer (nothing is pushed here: the stack stays 16-aligned) */
VPRIV codegen_value x64_alloca(codegen_context *e, codegen_value n)
{
	int r = x64_toreg(e, &n, 8);

	x64_record_emit_three_registers(e, X64_OP_ADDI, 8, r, r, X64_REGISTER_NONE)->immediate = 15;
	x64_record_emit_three_registers(e, X64_OP_ANDI, 8, r, r, X64_REGISTER_NONE)->immediate = -16;
	x64_record_emit_three_registers(e, X64_OP_SUB, 8, RSP, RSP, r);
	x64_mov(e, r, RSP);
	return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r };
}

VPRIV int x64_spget(codegen_context *e)
{
	int r = x64_treg(e);

	x64_mov(e, r, RSP);
	return r;
}

VPRIV void x64_spset(codegen_context *e, int r)
{
	x64_mov(e, RSP, r);
	x64_tfree(e, r);
}

VPRIV codegen_value x64_fbits(codegen_context *e, codegen_value v, int size)
{
	int r = x64_treg(e);

	x64_record_emit_three_registers(e, X64_OP_MOV, size <= 4 ? 4 : 8, r, v.register_number, X64_REGISTER_NONE);
	if (size == 2) x64_record_emit_three_registers(e, X64_OP_ANDI, 4, r, r, X64_REGISTER_NONE)->immediate = 0xffff;
	x64_tfree(e, v.register_number);
	return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r };
}

/* SSE4.2 string comparisons have fixed result registers and, with explicit
 * lengths, fixed integer inputs. Evaluate and park each argument first; then
 * preserve the enclosing expression's live registers across the instruction. */
VPRIV codegen_value x64_pcmpstr(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	const uint32_t *arg = &c->lists[x->b];
	cc_value cv;
	bool blend = x->aux == CC_BUILTIN_X86_BLENDVB;
	uint32_t fn = e->walk_state->inline_call ? e->walk_state->inline_call->function : e->walk_state->function_symbol;
	if (!x86_feature_has(cc_x86_features_get(c, fn), blend ? X86_FEATURE_SSE41 : X86_FEATURE_SSE42))
		codegen_fatal_report(e, x->loc, "builtin requires target feature '%s'", blend ? "sse4.1" : "sse4.2");
	if (!blend) cc_eval(c, arg[x->c - 1], &cv);
	unsigned mode = blend ? 1 : (unsigned)cv.i, kind = mode & 7;
	bool explicit = (mode & 8) != 0;
	unsigned count = blend ? 3 : explicit ? 4 : 2;
	if (!blend) cc_eval(c, arg[count], &cv);
	unsigned control = blend ? 0 : (unsigned)cv.i;
	int64_t slot[4], saved[32] = { 0 };
	uint32_t live = e->x64_context->live;
	uint32_t fixed = (1u << XMM0) | (1u << (XMM0 + 1)) | (blend ? (1u << (XMM0 + 2)) : (1u << RCX)) |
		(explicit ? (1u << RAX) | (1u << RDX) : 0);
	x64_nofp(e);
	for (unsigned i = 0; i < count; i++) {
		bool vector = !explicit || !(i & 1);
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = slot[i] = walk_slot_temporary(e, vector ? 16 : 4, vector ? 16 : 4) };
		codegen_value v = walk_value_emit(e, arg[i]);
		if (vector) {
			v = walk_lvalue_store_memory(e, &l, N(arg[i])->ty, v);
		} else {
			int r = walk_value_to_register(e, &v, codegen_type_primitive(AST_TYPE_INT));
			x64_store(e, r, &l, 4);
		}
		walk_value_free(e, &v);
	}
	for (int r = 0; r < 32; r++)
		if ((live & fixed) & (1u << r)) {
			int size = r >= XMM0 ? 16 : 8;
			codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = saved[r] = walk_slot_temporary(e, size, size) };
			x64_store(e, r, &l, size);
		}
	e->x64_context->live |= fixed;
	for (unsigned i = 0; i < count; i++) {
		bool vector = !explicit || !(i & 1);
		int r = blend ? XMM0 + (i == 2 ? 0 : (int)i + 1) :
			vector ? (i == 0 ? XMM0 : (XMM0 + 1)) : i == 1 ? RAX : RDX;
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = slot[i] };
		x64_load(e, r, &l, vector ? 16 : 4, false);
	}
	record *ins = x64_record_emit(e, blend ? X64_OP_BLENDVB : X64_OP_PCMPSTR);
	ins->condition = (explicit ? 0 : 2) | (kind == 1 ? 0 : 1);
	ins->immediate = control;
	codegen_value result;
	if (kind == 1) {
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, 16, 16) };
		x64_store(e, blend ? XMM0 + 1 : XMM0, &l, 16);
		result = (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)x64_laddr(e, &l) };
	} else {
		int r = x64_treg(e);
		if (kind == 0) x64_mov(e, r, RCX);
		else {
			static const uint8_t cc[] = { 0, 0, X64_CONDITION_A, X64_CONDITION_B, X64_CONDITION_O, X64_CONDITION_S, X64_CONDITION_E };
			ins = x64_record_emit_three_registers(e, X64_OP_SETCC, 4, r, X64_REGISTER_NONE, X64_REGISTER_NONE);
			ins->condition = cc[kind];
		}
		result = (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r };
	}
	for (int r = 0; r < 32; r++)
		if (saved[r]) {
			codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = saved[r] };
			x64_load(e, r, &l, r >= XMM0 ? 16 : 8, false);
		}
	e->x64_context->live = live | (1u << result.register_number);
	return result;
}
