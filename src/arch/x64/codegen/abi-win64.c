/* vcc code generator, x86-64: the Microsoft x64 calling convention (PLAN-win64.md WIN2), for
 * Windows x64 and for ms_abi functions on every x86-64 OS.  abi.c's entry points (the walker's
 * WALK_LOWER(classify), WALK_LOWER(call), ...) come here when e->msabi says the call, function or va_list is
 * Microsoft's; abi.c has System V's.
 *
 * Every argument is one 8-byte position: rcx, rdx, r8, r9 (xmm0-xmm3 for float and double) by
 * position, the rest on the stack above 32 bytes of shadow space the caller always reserves.
 * Records of 1, 2, 4 or 8 bytes travel as an integer; everything else (other records, empty ones,
 * long double, __int128, __float128, vectors) by reference to a copy the caller makes.  Results in
 * rax or xmm0; __int128 and vectors in xmm0; the rest through a hidden pointer (the first argument,
 * returned in rax).  Varargs: a floating-point argument goes in both its xmm register and the
 * integer one; the callee homes rcx..r9 to the shadow space, so va_list is a char * walking 8-byte
 * slots, reading a pointer for a by-reference type.  As clang (WinX86_64ABIInfo).
 *
 * The callee-saved set (rbx rbp rdi rsi r12-r15 xmm6-xmm15) is print.c's: its prologue saves those
 * the body writes.
 */
#include "x64-internal.h"

static const uint8_t x64w_gpr[4] = { RCX, RDX, R8, R9 };

VPRIV void x64_x87st(codegen_context *e, const codegen_lvalue *l);	/* fp.c */

VPRIV int x64w_argreg(int i) { return x64w_gpr[i]; }

/* the classes: n 1 one 8-byte word in a register (cls WALK_ARGUMENT_INTEGER or WALK_ARGUMENT_SSE); up: returned in xmm0
   (16 bytes), passed by reference; is_in_memory: by reference both ways; n 0 and not is_in_memory: nothing */
VPRIV void x64w_classify(codegen_context *e, ast_qualified_type t, walk_argument *a, bool unnamed)
{
	cc *c = e->compile_context;
	uint64_t size = codegen_type_size(c, t);
	int k = KIND(c, t);

	*a = (walk_argument){ .size = size, .is_unnamed = unnamed, .is_vector = codegen_type_is_vector(c, t), .align = codegen_type_alignment(c, t) > 8 ? codegen_type_alignment(c, t) : 8 };
	if (codegen_type_is_record(c, t) || codegen_type_is_array(c, t)) {
		/* "Any argument that doesn't fit in 8 bytes, or is not 1, 2, 4, or 8 bytes, must be
		   passed by reference" (an empty record too) */
		if (size == 1 || size == 2 || size == 4 || size == 8)
			a->eightbyte_count = 1, a->classes[0] = WALK_ARGUMENT_INTEGER;
		else
			a->is_in_memory = true;
		return;
	}
	if (codegen_type_is_vector(c, t) && T(c, t)->n == 1 && !codegen_type_is_bool_vector(c, t) && size <= 8) {
		/* one lane: LLVM scalarizes it, so the element's register (v1hi in rax, v1sf in xmm0) */
		a->eightbyte_count = 1;
		a->classes[0] = codegen_type_is_float(c, T(c, t)->base) ? WALK_ARGUMENT_SSE : WALK_ARGUMENT_INTEGER;
		return;
	}
	if (codegen_type_is_vector(c, t)) {
		if (size <= 16)	/* by reference, returned in xmm0; LLVM widens a short one to 16 bytes, and
				   reads it with an aligned 16-byte load */
			a->eightbyte_count = 1, a->classes[0] = WALK_ARGUMENT_SSE, a->is_sse_up = true, a->align = 16;
		else if (size % 16 == 0 && size <= 64)
			a->is_in_memory = a->is_sse_up = true;	/* (LLVM without AVX: a pointer per 16 bytes, returned in xmm0..) */
		else
			a->is_in_memory = true;
		return;
	}
	if (codegen_type_is_bitint_big(c, t)) {
		a->is_in_memory = true;
		return;
	}
	if ((k == AST_TYPE_INT128 || k == AST_TYPE_UINT128) && !codegen_type_is_bitint(c, t)) {	/* (_BitInt(65..128): by reference both ways) */
		a->eightbyte_count = 1, a->classes[0] = WALK_ARGUMENT_SSE, a->is_sse_up = true;	/* MinGW: returned in xmm0 */
		return;
	}
	if (k == AST_TYPE_LONG_DOUBLE && !CODEGEN_IS_WIN64(c)) {
		a->is_in_memory = a->is_x87 = true;	/* ms_abi elsewhere (no MinGW rule): by reference, returned in st(0) */
		return;
	}
	if (k == AST_TYPE_LONG_DOUBLE || k == AST_TYPE_FLOAT128 || size > 8) {
		a->is_in_memory = true;
		return;
	}
	a->eightbyte_count = 1;
	a->classes[0] = codegen_type_is_float(c, t) ? WALK_ARGUMENT_SSE : WALK_ARGUMENT_INTEGER;
	if (x64_target_has_no_simd(e) && a->classes[0] == WALK_ARGUMENT_SSE)
		x64_nofp(e);
}

/* a vector of 32 or 64 bytes: LLVM splits it in 16-byte halves, each by reference in its own
   position, and returns it in xmm0, xmm1 (...) */
static bool x64w_split(const walk_argument *a) { return a->is_in_memory && a->is_sse_up; }

/* the positions an argument takes */
static int x64w_npos(const walk_argument *a) { return x64w_split(a) ? (int)(a->size / 16) : 1; }

/* passed as a pointer to the caller's copy (the one object) */
static bool x64w_byref(const walk_argument *a) { return (a->is_in_memory || a->is_sse_up) && !x64w_split(a); }

static codegen_lvalue x64w_fr(int64_t off) { return (codegen_lvalue){ .kind = CODEGEN_LVALUE_FRAME, .offset = off, .align = 8 }; }

/* the first size bytes of xmm0 stored to the frame slot at off (X64_OP_ST) or loaded from the object at
   [r] (X64_OP_LD, off the register): a short vector result, no wider than it is */
VPRIV void x64w_xmm_part(codegen_context *e, int op, int64_t off, int size)
{
	codegen_lvalue m = op == X64_OP_ST ? x64w_fr(off) : (codegen_lvalue){ .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)off, .align = 1 };

	if (size == 8 || size == 4) {
		x64_record_emit_memory(e, op, size, XMM0, &m, false);
	} else if (op == X64_OP_ST) {
		x64_record_emit_three_registers(e, X64_OP_MOV, 4, R11, XMM0, X64_REGISTER_NONE);
		x64_record_emit_memory(e, X64_OP_ST, size, R11, &m, false);
	} else {
		x64_record_emit_memory(e, X64_OP_LD, size, R11, &m, false);
		x64_record_emit_three_registers(e, X64_OP_MOV, 4, XMM0, R11, X64_REGISTER_NONE);
	}
}

/* the live temporaries a Microsoft callee does not preserve, pushed (rsi and rdi survive, and the
   float temporaries xmm8-xmm14 are among xmm6-xmm15) */
static uint32_t x64w_save(codegen_context *e)
{
	static const uint8_t volatile_temps[] = { RAX, RCX, RDX, R8, R9, R10 };

	return x64_save(e, volatile_temps, sizeof volatile_temps, -1, -1);
}

/* the outgoing area: max(4, n) words, rsp 16-aligned at the call; its size */
static int64_t x64w_outgoing(codegen_context *e, int n)
{
	x64_lowering_context *X = e->x64_context;
	int64_t total = 8 * (int64_t)(n > 4 ? n : 4);

	if ((X->outstanding_pushes * 8 + total) % 16)
		total += 8;
	x64_record_emit(e, X64_OP_ADJSP)->immediate = -total;
	X->outstanding_pushes += (int)(total / 8);
	return total;
}

VPRIV codegen_value x64w_call(codegen_context *e, uint32_t ref, codegen_value *fn, const walk_argument *a, int n, bool variadic, const walk_argument *r, ast_qualified_type rt)
{
	x64_lowering_context *X = e->x64_context;
	int first = r && r->is_in_memory && !r->is_sse_up && !r->is_x87 ? 1 : 0;	/* the hidden result pointer takes position 0 */
	int np = first;
	uint32_t saved = x64w_save(e);
	record *i;

	for (int k = 0; k < n; k++)
		np += x64w_npos(&a[k]);
	int64_t total = x64w_outgoing(e, np);
	/* the stack words first: r11 is their scratch, and fn's register moves into it next */
	for (int k = 0, p = first; k < n; p += x64w_npos(&a[k]), k++)
		for (int j = 0; j < x64w_npos(&a[k]); j++) {
			codegen_lvalue s = x64w_fr(a[k].slot + 16 * j), d = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP, .offset = 8 * (p + j) };

			if (p + j < 4)
				continue;
			x64_record_emit_memory(e, x64w_byref(&a[k]) || x64w_split(&a[k]) ? X64_OP_LEA : X64_OP_LD, 8, R11, &s, false);
			x64_record_emit_memory(e, X64_OP_ST, 8, R11, &d, false);
		}
	if (fn)
		x64_mov(e, R11, fn->register_number);
	if (first) {
		codegen_lvalue s = x64w_fr(r->slot);

		x64_record_emit_memory(e, X64_OP_LEA, 8, RCX, &s, false);
	}
	for (int k = 0, p = first; k < n && p < 4; p += x64w_npos(&a[k]), k++) {
		codegen_lvalue s = x64w_fr(a[k].slot);

		if (x64w_split(&a[k])) {
			for (int j = 0; j < x64w_npos(&a[k]) && p + j < 4; j++) {
				codegen_lvalue h = x64w_fr(a[k].slot + 16 * j);

				x64_record_emit_memory(e, X64_OP_LEA, 8, x64w_gpr[p + j], &h, false);
			}
		} else if (x64w_byref(&a[k]) || (!a[k].eightbyte_count && !a[k].is_in_memory)) {
			x64_record_emit_memory(e, X64_OP_LEA, 8, x64w_gpr[p], &s, false);
		} else if (a[k].classes[0] == WALK_ARGUMENT_SSE) {
			x64_record_emit_memory(e, X64_OP_LD, 8, XMM0 + p, &s, false);
			if (variadic)	/* a variadic callee reads it from the integer register */
				x64_record_emit_memory(e, X64_OP_LD, 8, x64w_gpr[p], &s, false);
		} else {
			x64_record_emit_memory(e, X64_OP_LD, 8, x64w_gpr[p], &s, false);
		}
	}
	if (fn) {
		i = x64_record_emit_three_registers(e, X64_OP_CALLR, 8, X64_REGISTER_NONE, R11, X64_REGISTER_NONE);
	} else {
		i = x64_record_emit(e, X64_OP_CALL);
		i->reference = ref;
	}
	i->shift = 1;	/* a Microsoft callee: rsi, rdi and xmm6-xmm15 survive it (print.c) */
	x64_record_emit(e, X64_OP_ADJSP)->immediate = total;
	X->outstanding_pushes -= (int)(total / 8);
	if (r && r->is_x87) {
		codegen_lvalue d = x64w_fr(r->slot);

		x64_x87st(e, &d);
	} else if (r && r->is_sse_up && !r->is_in_memory && r->size < 16) {
		x64w_xmm_part(e, X64_OP_ST, r->slot, (int)r->size);
	} else if (r && !r->is_in_memory && r->eightbyte_count) {
		codegen_lvalue d = x64w_fr(r->slot);

		x64_record_emit_memory(e, X64_OP_ST, r->is_sse_up ? 16 : 8, r->classes[0] == WALK_ARGUMENT_SSE ? XMM0 : RAX, &d, false);
	} else if (r && x64w_split(r)) {
		x64_call_xmms(e, r);
	} else if (!r) {
		x64_call_park(e, rt);
	}
	x64_restore(e, saved);
	return x64_call_value(e, fn, rt, r);
}

/*
 * The incoming side.  Position p's word is in its register (p < 4) or at [rbp + 16 + 8p].  A
 * by-reference parameter is used in place: its slot holds the address (x64_byref, the walker).
 */
VPRIV void x64w_params(codegen_context *e, walk_argument *a, int n, const walk_argument *r, bool variadic)
{
	int first = r && r->is_in_memory && !r->is_sse_up && !r->is_x87 ? 1 : 0;
	int64_t half[4] = { 0 };	/* a split vector's half pointer from a register, parked in a slot */
	cc *c = e->compile_context;
	x64_lowering_context *X = e->x64_context;
	const node *body = N(c->syms[e->walk_state->function_symbol].body);

	X->interrupt = 0;
	X->result_pointer_slot = 0;
	X->variadic_save_slot = 0;
	if (variadic)	/* the register words to the shadow space: va_arg reads them all from there
			   (a float's too: a variadic call passes it in both registers) */
		for (int p = 0; p < 4; p++) {
			codegen_lvalue d = x64w_fr(16 + 8 * p);

			x64_record_emit_memory(e, X64_OP_ST, 8, x64w_gpr[p], &d, false);
		}
	if (first) {
		codegen_lvalue d = x64w_fr(r->slot);

		x64_record_emit_memory(e, X64_OP_ST, 8, RCX, &d, false);
		X->result_pointer_slot = r->slot;
	}
	for (int k = 0, p = first; k < n; p += x64w_npos(&a[k]), k++) {
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		ast_qualified_type ty = c->syms[sym].ty;
		codegen_lvalue home;
		bool local = x64_local(e, sym, &home);

		if (x64w_split(&a[k])) {	/* (copied into the slot below, once the registers are stored) */
			for (int j = 0; j < x64w_npos(&a[k]) && p + j < 4; j++) {
				codegen_lvalue d = x64w_fr(half[p + j] = walk_slot_temporary(e, 8, 8));

				x64_record_emit_memory(e, X64_OP_ST, 8, x64w_gpr[p + j], &d, false);
			}
			continue;
		}
		if (x64w_byref(&a[k])) {	/* (x64_byref: the slot holds the caller copy's address) */
			codegen_lvalue d = x64w_fr(a[k].slot);

			if (p < 4)
				x64_record_emit_memory(e, X64_OP_ST, 8, x64w_gpr[p], &d, false);
			else
				a[k].slot = 16 + 8 * p;
			continue;
		}
		if (p < 4) {
			int from = a[k].classes[0] == WALK_ARGUMENT_SSE ? XMM0 + p : x64w_gpr[p];

			if (local && from < XMM0) {
				codegen_lvalue v = { .kind = CODEGEN_LVALUE_LOCAL_REGISTER, .register_number = (uint8_t)from };

				x64_load(e, home.register_number, &v, walk_type_size(c, ty), walk_type_is_signed(c, ty));
			} else {
				codegen_lvalue d = x64w_fr(a[k].slot);

				x64_record_emit_memory(e, X64_OP_ST, 8, from, &d, false);
			}
			continue;
		}
		/* on the stack: the parameter is where the caller put it */
		a[k].slot = 16 + 8 * p;
		if (local) {
			codegen_lvalue from = x64w_fr(a[k].slot);

			x64_load(e, home.register_number, &from, walk_type_size(c, ty), walk_type_is_signed(c, ty));
		}
	}
	int np = first;

	for (int k = 0; k < n; np += x64w_npos(&a[k]), k++)
		for (int j = 0; x64w_split(&a[k]) && j < x64w_npos(&a[k]); j++) {
			codegen_lvalue s = x64w_fr(np + j < 4 ? half[np + j] : 16 + 8 * (np + j)), d = x64w_fr(a[k].slot + 16 * j);
			int rs = x64_treg(e), rd;

			x64_record_emit_memory(e, X64_OP_LD, 8, rs, &s, false);
			rd = x64_laddr(e, &d);
			x64_copy(e, rd, rs, 16);
			x64_tfree(e, rd);
			x64_tfree(e, rs);
		}
	X->general_register_arguments = np, X->float_register_arguments = 0, X->stack_argument_bytes = 0;
	if (variadic)
		X->variadic_save_slot = 16;	/* (va_start: the named words are below the first unnamed one) */
	if (CODEGEN_IS_WIN64(c)) {
		/* MinGW's main calls __main first (the constructors), as clang */
		const sym *y = &c->syms[e->walk_state->function_symbol];

		if (y->sc != AST_STORAGE_STATIC && y->name && CODEGEN_IDENTIFIER_IS(y->name, "main")) {
			e->has_calls = true;
			x64w_call(e, CODEGEN_FIXED_REFERENCE(FN___MAIN), NULL, NULL, 0, false, NULL, codegen_type_primitive(AST_TYPE_VOID));
		}
	}
}

/* va_list (a char * variable) at rap: the first unnamed argument's word */
VPRIV void x64w_va_start(codegen_context *e, int rap)
{
	codegen_lvalue first = x64w_fr(16 + 8 * e->x64_context->general_register_arguments), ap = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rap };
	int r = x64_laddr(e, &first);

	x64_store(e, r, &ap, 8);
	x64_tfree(e, r);
}

/* the address of the next argument of type t: its 8-byte word, or the object it points to */
VPRIV int x64w_va_arg(codegen_context *e, int rap, ast_qualified_type t)
{
	walk_argument a;
	codegen_lvalue ap = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rap };
	int ra = x64_treg(e);
	codegen_lvalue nx = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)ra, .offset = 8 };

	x64w_classify(e, t, &a, true);
	x64_record_emit_memory(e, X64_OP_LD, 8, ra, &ap, false);
	x64_record_emit_memory(e, X64_OP_LEA, 8, R11, &nx, false);
	x64_record_emit_memory(e, X64_OP_ST, 8, R11, &ap, false);
	if (x64w_byref(&a)) {
		codegen_lvalue p = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)ra };

		x64_record_emit_memory(e, X64_OP_LD, 8, ra, &p, false);
	}
	return ra;
}

/*
 * A runtime helper (__divti3, __addtf3, __fixdfti, ...) as compiler-rt is built for the target:
 * the Microsoft convention.  in[k] is the frame slot of argument k (8-byte words; a by-reference
 * type's object), types[k] its type; the result (type rt) is stored to the frame slot res (at
 * least 8 bytes; 16 for __int128, __float128, long double).
 */
VPRIV void x64w_helper(codegen_context *e, int fn, const int64_t *in, const ast_qualified_type *types, int n, int64_t res, ast_qualified_type rt)
{
	walk_argument a[4], r;

	for (int k = 0; k < n; k++) {
		x64w_classify(e, types[k], &a[k], false);
		a[k].slot = in[k];
	}
	x64w_classify(e, rt, &r, false);
	r.slot = res;
	e->has_calls = true;
	x64w_call(e, CODEGEN_FIXED_REFERENCE(fn), NULL, a, n, false, &r, rt);
}
