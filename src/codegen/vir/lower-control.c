/* The walker's lowering interface for builtins and control: atomics, varargs, labels, jumps; what the builder does not handle. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* ---------------- builtins and the rest ---------------- */

VPRIV val vw_bitop(ecx *e, int bi, int size, val a)
{
	uint32_t ty = vw_ity(size), x = vw_val(e, &a, ty), op;
	int r = vw_resname(e, &a, NULL);

	switch (bi) {
	case CC_BUILTIN_CLZ: case CC_BUILTIN_CLZL: case CC_BUILTIN_CLZLL: op = VIR_OP_CLZ; break;
	case CC_BUILTIN_CTZ: case CC_BUILTIN_CTZL: case CC_BUILTIN_CTZLL: op = VIR_OP_CTZ; break;
	case CC_BUILTIN_POPCOUNT: case CC_BUILTIN_POPCOUNTL: case CC_BUILTIN_POPCOUNTLL: op = VIR_OP_POPCNT; break;
	case CC_BUILTIN_BSWAP32: case CC_BUILTIN_BSWAP64: op = VIR_OP_BSWAP; break;
	case CC_BUILTIN_BSWAP16:
		x = vw_ins(e, VIR_OP_BSWAP, VIR_TYPE_I32, 0, vw_conv(e, x, VIR_TYPE_I32), 0, 0);
		vw_set(e, r, vw_ins(e, VIR_OP_LSHR, VIR_TYPE_I32, 0, x, vw_iconst(e, VIR_TYPE_I32, 16), 0));
		return (val){ .k = V_REG, .r = (uint8_t)r };
	case CC_BUILTIN_PARITY: case CC_BUILTIN_PARITYL: case CC_BUILTIN_PARITYLL:
		x = vw_ins(e, VIR_OP_POPCNT, ty, 0, x, 0, 0);
		vw_set(e, r, vw_ins(e, VIR_OP_AND, ty, 0, x, vw_iconst(e, ty, 1), 0));
		return (val){ .k = V_REG, .r = (uint8_t)r };
	default: vw_fail(e, VIRFAIL(unsupported_builtin));
	}
	vw_set(e, r, vw_ins(e, op, ty, 0, x, 0, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

/* sequentially consistent; a result is zero-extended (an i32 for 4 bytes or less) */
VPRIV int vw_atomic(ecx *e, int op, int size, int rp, int rv, int rexp)
{
	uint32_t ty = vw_ity(size), fl = vw_mfl(size, (uint32_t)size, false), p, v = 0;
	int r;

	if (op == WK_AFENCE) {
		vw_ins(e, VIR_OP_AFENCE, VIR_TYPE_VOID, 0, 0, 0, 0);
		vw_st_clear(e, true);
		return -1;
	}
	p = vw_get(e, rp, VIR_TYPE_PTR);
	if (op != WK_ALOAD)
		v = vw_get(e, rv, ty);
	vw_st_clear(e, true);
	switch (op) {
	case WK_ALOAD: v = vw_ins(e, VIR_OP_ALOAD, ty, fl, p, 0, 0); break;
	case WK_ASTORE: vw_ins(e, VIR_OP_ASTORE, VIR_TYPE_VOID, fl, p, v, 0); return -1;
	case WK_AXCHG: v = vw_ins(e, VIR_OP_AXCHG, ty, fl, p, v, 0); break;
	case WK_AXADD: v = vw_ins(e, VIR_OP_AXADD, ty, fl, p, v, 0); break;
	default: v = vw_ins(e, VIR_OP_ACAS, ty, fl, p, vw_get(e, rexp, ty), v); break;
	}
	r = vw_treg(e);
	vw_set(e, r, v);
	if (size < 4)	/* (the narrow value zero-extended, as the hardware's) */
		vw_ext(e, r, size, false);
	return r;
}

VPRIV int vw_tlsaddr(ecx *e, uint32_t ref, int model) { (void)ref, (void)model; vw_fail(e, VIRFAIL(thread_local)); }
VPRIV val vw_frame(ecx *e, bool ret, uint32_t level) { (void)ret, (void)level; vw_fail(e, VIRFAIL(frame_address)); }
VPRIV val vw_alloca(ecx *e, val n) { (void)n; vw_fail(e, VIRFAIL(alloca)); }
VPRIV int vw_spget(ecx *e) { vw_fail(e, VIRFAIL(stack_pointer)); }
VPRIV void vw_spset(ecx *e, int r) { (void)r; vw_fail(e, VIRFAIL(stack_pointer)); }
VPRIV val vw_setjmp(ecx *e, int rbuf) { (void)rbuf; vw_fail(e, VIRFAIL(setjmp)); }
VPRIV void vw_longjmp(ecx *e, int rbuf) { (void)rbuf; vw_fail(e, VIRFAIL(longjmp)); }
VPRIV void vw_asm(ecx *e, uint32_t n) { (void)n; vw_fail(e, VIRFAIL(inline_assembly)); }
/* the va_list at rap points at the first unnamed argument (Apple's: on the stack) */
VPRIV void vw_va_start(ecx *e, int rap)
{
	if (!DARWIN(e->c))
		vw_fail(e, VIRFAIL(variadic_start));
	vw_ins(e, VIR_OP_VA_START, VIR_TYPE_VOID, 0, vw_get(e, rap, VIR_TYPE_PTR), 0, 0);
	vw_st_clear(e, true);
}
/* the address of the next argument of va_list *rap, which moves past it (as va_arg_ in
 * arch/a64/codegen/builtin.c); Apple's va_list is a pointer into the stack arguments,
 * 8 bytes each (16-aligned ones aligned first, records over 16 bytes by reference) */
VPRIV int vw_va_arg(ecx *e, int rap, ast_qualified_type t)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	bool big = is_recabi(c, t) && size > 16;
	lval ap = { .k = L_REG, .r = (uint8_t)rap, .align = 8 };
	uint32_t p, q;
	int r, rn;

	if (!DARWIN(c))
		vw_fail(e, VIRFAIL(variadic_argument));
	r = vw_treg(e);
	vw_load(e, r, &ap, 8, false);
	p = vw_get(e, r, VIR_TYPE_PTR);
	if (size && !big && abi_align(c, t) == 16) {
		q = vw_ins(e, VIR_OP_P2I, VIR_TYPE_I64, 0, p, 0, 0);
		q = vw_ins(e, VIR_OP_ADD, VIR_TYPE_I64, 0, q, vw_iconst(e, VIR_TYPE_I64, 15), 0);
		q = vw_ins(e, VIR_OP_AND, VIR_TYPE_I64, 0, q, vw_iconst(e, VIR_TYPE_I64, -16), 0);
		p = vw_ins(e, VIR_OP_I2P, VIR_TYPE_PTR, 0, q, 0, 0);
	}
	q = vw_ins(e, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, p, vw_iconst(e, VIR_TYPE_I64, big ? 8 : (int64_t)((size + 7) & ~(uint64_t)7)), 0);
	rn = vw_treg(e);
	vw_set(e, rn, q);
	vw_store(e, rn, &ap, 8);
	vw_tfree(e, rn);
	vw_set(e, r, p);
	if (big) {
		lval m = { .k = L_REG, .r = (uint8_t)r, .align = 8 };

		vw_load(e, r, &m, 8, false);
	}
	return r;
}
VPRIV void vw_retrec(ecx *e, int raddr, const wkarg *r, uint32_t lbl) { (void)raddr, (void)r, (void)lbl; vw_fail(e, VIRFAIL(record_result)); }
VPRIV bool vw_tail_return(ecx *e, uint32_t expr) { (void)e, (void)expr; return false; }

VPRIV void vw_trap(ecx *e)
{
	vw_term(e, VIR_OP_TRAP, 0, 0, 0, 0);
}

VPRIV void vw_barrier(ecx *e)
{
	vw_ins(e, VIR_OP_BARRIER, VIR_TYPE_VOID, 0, 0, 0, 0);
	vw_st_clear(e, true);
}

VPRIV void vw_dbgrow(ecx *e, uint32_t loc) { e->curloc = loc; }

VPRIV void vw_label(ecx *e, uint32_t l)
{
	vwst *V = e->vw;
	uint32_t b = vw_lblk(e, l);

	e->reach = true;
	V->b[V->cur].term = vw_put(e, VIR_OP_BR, VIR_TYPE_VOID, 0, b, 0, 0, V->cur);
	V->cur = b;
	V->nst = 0;
	wk_ael_clear(e, UINT32_MAX);
}

VPRIV void vw_jump(ecx *e, uint32_t l) { vw_br(e, vw_lblk(e, l)); }
VPRIV void vw_jumpr(ecx *e, int r) { (void)r; vw_fail(e, VIRFAIL(computed_goto)); }
VPRIV void vw_named(ecx *e, uint32_t ref) { (void)ref; vw_fail(e, VIRFAIL(label_address)); }
