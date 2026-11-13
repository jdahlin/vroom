/* The walker's lowering interface for builtins and control: atomics, varargs, labels, jumps; what the builder does not handle. */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* ---------------- builtins and the rest ---------------- */

VPRIV val virbuild_bitop(ecx *e, int bi, int size, val a)
{
	uint32_t ty = virbuild_integer_type(size), x = virbuild_walker_value_get(e, &a, ty), op;
	int r = virbuild_result_register_pick(e, &a, NULL);

	switch (bi) {
	case BI_CLZ: case BI_CLZL: case BI_CLZLL: op = VIR_OP_CLZ; break;
	case BI_CTZ: case BI_CTZL: case BI_CTZLL: op = VIR_OP_CTZ; break;
	case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL: op = VIR_OP_POPCNT; break;
	case BI_BSWAP32: case BI_BSWAP64: op = VIR_OP_BSWAP; break;
	case BI_BSWAP16:
		x = virbuild_insn_append(e, VIR_OP_BSWAP, VIR_TYPE_I32, 0, virbuild_value_convert(e, x, VIR_TYPE_I32), 0, 0);
		virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_LSHR, VIR_TYPE_I32, 0, x, virbuild_constant_integer(e, VIR_TYPE_I32, 16), 0));
		return (val){ .k = V_REG, .r = (uint8_t)r };
	case BI_PARITY: case BI_PARITYL: case BI_PARITYLL:
		x = virbuild_insn_append(e, VIR_OP_POPCNT, ty, 0, x, 0, 0);
		virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_AND, ty, 0, x, virbuild_constant_integer(e, ty, 1), 0));
		return (val){ .k = V_REG, .r = (uint8_t)r };
	default: virbuild_fail(e, VIRBUILD_FAILURE(unsupported_builtin));
	}
	virbuild_register_write(e, r, virbuild_insn_append(e, op, ty, 0, x, 0, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

/* sequentially consistent; a result is zero-extended (an i32 for 4 bytes or less) */
VPRIV int virbuild_atomic(ecx *e, int op, int size, int rp, int rv, int rexp)
{
	uint32_t ty = virbuild_integer_type(size), fl = virbuild_memory_flags_make(size, (uint32_t)size, false), p, v = 0;
	int r;

	if (op == WK_AFENCE) {
		virbuild_insn_append(e, VIR_OP_AFENCE, VIR_TYPE_VOID, 0, 0, 0, 0);
		virbuild_store_table_clear(e, true);
		return -1;
	}
	p = virbuild_register_read(e, rp, VIR_TYPE_PTR);
	if (op != WK_ALOAD)
		v = virbuild_register_read(e, rv, ty);
	virbuild_store_table_clear(e, true);
	switch (op) {
	case WK_ALOAD: v = virbuild_insn_append(e, VIR_OP_ALOAD, ty, fl, p, 0, 0); break;
	case WK_ASTORE: virbuild_insn_append(e, VIR_OP_ASTORE, VIR_TYPE_VOID, fl, p, v, 0); return -1;
	case WK_AXCHG: v = virbuild_insn_append(e, VIR_OP_AXCHG, ty, fl, p, v, 0); break;
	case WK_AXADD: v = virbuild_insn_append(e, VIR_OP_AXADD, ty, fl, p, v, 0); break;
	default: v = virbuild_insn_append(e, VIR_OP_ACAS, ty, fl, p, virbuild_register_read(e, rexp, ty), v); break;
	}
	r = virbuild_treg(e);
	virbuild_register_write(e, r, v);
	if (size < 4)	/* (the narrow value zero-extended, as the hardware's) */
		virbuild_ext(e, r, size, false);
	return r;
}

VPRIV int virbuild_tlsaddr(ecx *e, uint32_t ref, int model) { (void)ref, (void)model; virbuild_fail(e, VIRBUILD_FAILURE(thread_local)); }
VPRIV val virbuild_frame(ecx *e, bool ret, uint32_t level) { (void)ret, (void)level; virbuild_fail(e, VIRBUILD_FAILURE(frame_address)); }
VPRIV val virbuild_alloca(ecx *e, val n) { (void)n; virbuild_fail(e, VIRBUILD_FAILURE(alloca)); }
VPRIV int virbuild_spget(ecx *e) { virbuild_fail(e, VIRBUILD_FAILURE(stack_pointer)); }
VPRIV void virbuild_spset(ecx *e, int r) { (void)r; virbuild_fail(e, VIRBUILD_FAILURE(stack_pointer)); }
VPRIV val virbuild_setjmp(ecx *e, int rbuf) { (void)rbuf; virbuild_fail(e, VIRBUILD_FAILURE(setjmp)); }
VPRIV void virbuild_longjmp(ecx *e, int rbuf) { (void)rbuf; virbuild_fail(e, VIRBUILD_FAILURE(longjmp)); }
VPRIV void virbuild_asm(ecx *e, uint32_t n) { (void)n; virbuild_fail(e, VIRBUILD_FAILURE(inline_assembly)); }
/* the va_list at rap points at the first unnamed argument (Apple's: on the stack) */
VPRIV void virbuild_va_start(ecx *e, int rap)
{
	if (!DARWIN(e->c))
		virbuild_fail(e, VIRBUILD_FAILURE(variadic_start));
	virbuild_insn_append(e, VIR_OP_VA_START, VIR_TYPE_VOID, 0, virbuild_register_read(e, rap, VIR_TYPE_PTR), 0, 0);
	virbuild_store_table_clear(e, true);
}
/* the address of the next argument of va_list *rap, which moves past it (as va_arg_ in
 * arch/a64/codegen/builtin.c); Apple's va_list is a pointer into the stack arguments,
 * 8 bytes each (16-aligned ones aligned first, records over 16 bytes by reference) */
VPRIV int virbuild_va_arg(ecx *e, int rap, qty t)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	bool big = is_recabi(c, t) && size > 16;
	lval ap = { .k = L_REG, .r = (uint8_t)rap, .align = 8 };
	uint32_t p, q;
	int r, rn;

	if (!DARWIN(c))
		virbuild_fail(e, VIRBUILD_FAILURE(variadic_argument));
	r = virbuild_treg(e);
	virbuild_load(e, r, &ap, 8, false);
	p = virbuild_register_read(e, r, VIR_TYPE_PTR);
	if (size && !big && abi_align(c, t) == 16) {
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
		lval m = { .k = L_REG, .r = (uint8_t)r, .align = 8 };

		virbuild_load(e, r, &m, 8, false);
	}
	return r;
}
VPRIV void virbuild_retrec(ecx *e, int raddr, const wkarg *r, uint32_t lbl) { (void)raddr, (void)r, (void)lbl; virbuild_fail(e, VIRBUILD_FAILURE(record_result)); }
VPRIV bool virbuild_tail_return(ecx *e, uint32_t expr) { (void)e, (void)expr; return false; }

VPRIV void virbuild_trap(ecx *e)
{
	virbuild_block_terminate(e, VIR_OP_TRAP, 0, 0, 0, 0);
}

VPRIV void virbuild_barrier(ecx *e)
{
	virbuild_insn_append(e, VIR_OP_BARRIER, VIR_TYPE_VOID, 0, 0, 0, 0);
	virbuild_store_table_clear(e, true);
}

VPRIV void virbuild_dbgrow(ecx *e, uint32_t loc) { e->curloc = loc; }

VPRIV void virbuild_label(ecx *e, uint32_t l)
{
	virbuild_context *V = e->vw;
	uint32_t b = virbuild_label_block(e, l);

	e->reach = true;
	V->b[V->cur].term = virbuild_preir_insn_add(e, VIR_OP_BR, VIR_TYPE_VOID, 0, b, 0, 0, V->cur);
	V->cur = b;
	V->nst = 0;
	wk_ael_clear(e, UINT32_MAX);
}

VPRIV void virbuild_jump(ecx *e, uint32_t l) { virbuild_branch_emit(e, virbuild_label_block(e, l)); }
VPRIV void virbuild_jumpr(ecx *e, int r) { (void)r; virbuild_fail(e, VIRBUILD_FAILURE(computed_goto)); }
VPRIV void virbuild_named(ecx *e, uint32_t ref) { (void)ref; virbuild_fail(e, VIRBUILD_FAILURE(label_address)); }
